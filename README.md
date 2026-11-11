# Motor Control

This zephyr project is just for me to mess around with motor control firmware using off the shelf hardware.

# Hardware

I have a Nucleo G474RE as the main development board cause it was the only one I had laying around. Plus it actually is very decent for motor control with its advance timers and ADCs. Its CPU is not that impressive but it will do since its only going to do motor control algorithms.

For the motor bridge, I bought a cheap amazon controller in the arduino shield form factor.

For the motor, I bought another cheap thing.

Look at `/docs` for more details cause I am too lazy to type more

# Structure

`/docs` for all the datasheets I need
`/apps` for firmware I am going to flash
`/host` for helpers outside of firmware (ie, graphing tools that run on my computer reading UART) <- only thing written by AI since who has time for that

and more I guess

# Setting up Zephyr

Make a new dir and call it something workspace. In there you will clone this repo and run the following:

```bash
cd <your workspace dir>

git clone <this repo>

python3 -m venv .venv
source .venv/bin/activate

python -m pip install --upgrade pip
python -m pip install west

west init -l motor-control
west update
west packages pip --install
west zephyr-export
```

# Collaborating and Licensing

Dont collaborate, tks