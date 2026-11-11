
#include "zephyr/sys/printk.h"
#include <stdio.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/pwm.h>

#include <math.h>

#define LED0_NODE DT_ALIAS(led0)
#define REPORTING_CONSOLE_NODE DT_ALIAS(motor_report)
#define MOTOR_POS_NODE DT_ALIAS(position_sensor)
#define PWM_PHASE_A_NODE DT_ALIAS(phase_a_pwm)
#define PWM_PHASE_B_NODE DT_ALIAS(phase_b_pwm)
#define PWM_PHASE_C_NODE DT_ALIAS(phase_c_pwm)
#define MOTOR_ENABLE_NODE DT_ALIAS(motor_enable)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *reporting_console = DEVICE_DT_GET(REPORTING_CONSOLE_NODE);
static const struct device *motor_pos_sensor = DEVICE_DT_GET(MOTOR_POS_NODE);
static const struct pwm_dt_spec pwm_phase_a = PWM_DT_SPEC_GET(PWM_PHASE_A_NODE);
static const struct pwm_dt_spec pwm_phase_b = PWM_DT_SPEC_GET(PWM_PHASE_B_NODE);
static const struct pwm_dt_spec pwm_phase_c = PWM_DT_SPEC_GET(PWM_PHASE_C_NODE);
static const struct gpio_dt_spec motor_enable = GPIO_DT_SPEC_GET(MOTOR_ENABLE_NODE, gpios);

// ---- Thread Stuff -------------------

static struct k_thread reporting_thread_data;
K_THREAD_STACK_DEFINE(reporting_thread_stack, 1028);

static struct k_thread motor_control_thread_data;
K_THREAD_STACK_DEFINE(motor_control_thread_stack, 1028);

// ---- Stuff about math and things -----
#define PI 3.14159265358979323846f //cause we are poor and math.h charges per import
#define TWO_PI (2.0f * (float)PI)

#define PWM_UPDATE_HZ 1000
#define ELECTRICAL_FREQUENCY_HZ 1

#define PWM_FREQUENCY_HZ 20000
#define PWM_PERIOD_NS 50000

#define PHASE_120_DEGREES (TWO_PI / 3.0f)
#define PHASE_240_DEGREES ((2.0f * TWO_PI) / 3.0f)

float theta = 0.0f;

float duty_max = 25.0f;
float desired_electrical_frequency_hz = 150.0f;

float frequency_acceleration_hz = 0.01f;

float duty_a;
float duty_b;
float duty_c;

int report_counter = 0;


void send_report(const char *msg){
    while(*msg != '\0'){
        uart_poll_out(reporting_console, *msg);
        msg++;
    }
}

void update_pwms(void){
    static float electrical_frequency_hz = 0;

    duty_a = 50 + duty_max * sinf(theta);
    duty_b = 50 + duty_max * sinf(theta + PHASE_120_DEGREES);
    duty_c = 50 + duty_max * sinf(theta + PHASE_240_DEGREES);

    uint32_t pulse_a = (uint32_t)(PWM_PERIOD_NS * duty_a / 100.0f);
    uint32_t pulse_b = (uint32_t)(PWM_PERIOD_NS * duty_b / 100.0f);
    uint32_t pulse_c = (uint32_t)(PWM_PERIOD_NS * duty_c / 100.0f);



    // set the PWM to the new duty
    pwm_set_pulse_dt(&pwm_phase_a, pulse_a);
    pwm_set_pulse_dt(&pwm_phase_b, pulse_b);
    pwm_set_pulse_dt(&pwm_phase_c, pulse_c);

    theta += TWO_PI * ( electrical_frequency_hz / (float)PWM_UPDATE_HZ );

    if (electrical_frequency_hz < desired_electrical_frequency_hz){
        electrical_frequency_hz += frequency_acceleration_hz;
    }

    // something about a circle only being 360 degrees
    if (theta > TWO_PI){
        theta -= TWO_PI;
    }
}

// void blink_thread(void){
//     int ret;
//     if (!gpio_is_ready_dt(&led)) {
//         return 0;
//     }

//     ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
//     if (ret < 0) {
//         return 0;
//     }

//     while(1){
//         gpio_pin_toggle_dt(&led);
//         k_sleep(K_MSEC(500));
//     }
// }

int motor_init(void){
    if (!pwm_is_ready_dt(&pwm_phase_a) ||
        !pwm_is_ready_dt(&pwm_phase_b) ||
        !pwm_is_ready_dt(&pwm_phase_c)) {
        printk("PWM device not ready\n");
        return -EIO;
    }

    if (!gpio_is_ready_dt(&motor_enable)) {
        printk("Motor enable GPIO not ready\n");
        return -EIO;
    }

    int ret = gpio_pin_configure_dt(&motor_enable, GPIO_OUTPUT_INACTIVE);

    if (ret < 0) {
        printk("Motor enable configuration failed: %d\n", ret);
        return -EIO;
    }

    return 0;
}

void reporting_thread(void *p1, void *p2, void *p3){
    // here report angle and currents (no PWM setting yet since I dont wanna handle thread safe sharing PWM stuff)
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct sensor_value angle;
    char report_buff[512];

    while (1) {
        int ret = sensor_sample_fetch(motor_pos_sensor);

        if (ret < 0){
            printk("Failed to fetch sensor data %d\n", ret);
        }
        else{
            ret = sensor_channel_get(motor_pos_sensor, SENSOR_CHAN_ROTATION, &angle);


            if(ret < 0){
                printk("Error getting the channel %d\n", ret);
            }
            else{
                snprintk(report_buff, sizeof(report_buff), "angle: %d.%06d degrees\r\n", angle.val1, angle.val2);
                send_report(report_buff);
            }

        }
        k_sleep(K_MSEC(100));
    }
}

void motor_control_thread(void *p1, void *p2, void *p3){
    // here we update the motor itself
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    // init the PWM and set the motor enable to high

    //start all PWM at 50%
    pwm_set_dt(&pwm_phase_a, PWM_PERIOD_NS, 0);
    pwm_set_dt(&pwm_phase_b, PWM_PERIOD_NS, 0);
    pwm_set_dt(&pwm_phase_c, PWM_PERIOD_NS, 0);

    gpio_pin_set_dt(&motor_enable, 1);

    while(1){
        update_pwms();
        k_sleep(K_MSEC(1));
    }

}

int main(void)
{
    int ret;

    if(!device_is_ready(reporting_console)){
        printk("UART4 is not ready\n");
        return 0;
    }

    if (!device_is_ready(motor_pos_sensor)){
        printk("Motor Position Sensor is not ready\n");
        return 0;
    }

    char buff[128];
ret = motor_init();

    if (ret < 0){
        printk("Error initializing motor stuff\n");
        return 0;
    }

    printk("AS5600 isreporting_thread_data ready\n");
    printk("Motor is ready\n");

    k_thread_create(&reporting_thread_data, reporting_thread_stack, K_THREAD_STACK_SIZEOF(reporting_thread_stack), reporting_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_create(&motor_control_thread_data, motor_control_thread_stack, K_THREAD_STACK_SIZEOF(motor_control_thread_stack), motor_control_thread, NULL, NULL, NULL, 7, 0, K_NO_WAIT);


    return 0;
}
