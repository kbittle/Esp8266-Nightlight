# How to setup environment:

Follow these steps to setup the espressif SDK and python virtual environment setup.

## Step 1: Install Required Ubuntu Dependencies

Open your WSL Ubuntu terminal and run:

```Bash
# Update package list
sudo apt update

# Install build tools, python, and menuconfig dependencies
sudo apt install -y git wget make libncurses-dev flex bison gperf \
                    python3 python3-pip python3-venv python3-serial
```

## Step 2: Download the ESP8266 Toolchain

The ESP8266 uses a custom Tensilica core, so standard gcc won't work. You need `xtensa-lx106-elf-gcc`:  

```Bash
# Create a dedicated directory in your home folder
mkdir -p ~/esp
cd ~/esp

# Download the 64-bit Linux Xtensa toolchain
wget https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz

# Extract it
tar -xzf xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz
```

## Step 3: Clone the ESP8266 RTOS SDK

```Bash
cd ~/esp

# Clone the SDK repository recursively (to pull submodules)
git clone --recursive https://github.com/espressif/ESP8266_RTOS_SDK.git
```

## Step 4: Install Python Dependencies & Configure Paths

Add the toolchain and SDK path (IDF_PATH) to your bash profile so WSL remembers them every session:

```Bash
# Add toolchain and IDF_PATH to environment
echo 'export PATH="$HOME/esp/xtensa-lx106-elf/bin:$PATH"' >> ~/.bashrc
echo 'export IDF_PATH="$HOME/esp/ESP8266_RTOS_SDK"' >> ~/.bashrc

# Reload profile
source ~/.bashrc

# Ensure the python venv package is installed
sudo apt update && sudo apt install -y python3-venv python3-full

# Create a virtual environment named "esp-env" inside ~/esp
python3 -m venv ~/esp/esp-env

# Activate the virtual environment
source ~/esp/esp-env/bin/activate

# Now install the requirements (it will work without error)
pip install -r $IDF_PATH/requirements.txt

pip install "setuptools<82"
```

## Step 5: Test Building Your Project

To test if everything was set up correctly, try building your project or one of the SDK's built-in examples:

```Bash
# Navigate to your project directory (or test on an example)
cd ~/esp/ESP8266_RTOS_SDK/examples/get-started/hello_world

# Configure build settings (optional)
make menuconfig

# Build the app using all available CPU cores
make -j$(nproc)
```

---

# Building nightlight project

Use makefile to build project. Output will be under build folder.

```Bash
cd ../esp-nightlight-project
source ~/esp/esp-env/bin/activate
make -j$(nproc) CONFIG_SDK_PYTHON=python3
```

# Docker Container Build

Instead of natively setting up all these tools. I created a docker container with the environment already setup. This is under the docker folder. 

To build this:
- Go to actions tab under github project repo
- Select "Publish Docker Build Image" workflow
- Hit "Run workflow". 

This will publish the built container to the users container registry. Update build.yml with the install path.
