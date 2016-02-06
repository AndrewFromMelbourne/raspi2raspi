# raspi2raspi
Program to copy from one Raspberry Pi display to another Raspberry Pi
display.
# usage

    raspi2raspi <options>

    --daemon - start in the background as a daemon
    --source <number> - Raspberry Pi display number (default 0)
    --destination <number> - Raspberry Pi display number (default 5)
    --fps <fps> - set desired frames per second (default 10 frames per second)
    --pidfile <pidfile> - create and lock PID file (if being run as a daemon)
    --help - print usage and exit

# build prerequisites
## cmake
You will need to install cmake

    sudo apt-get install cmake
## libraries
You will need to install libbsd-dev

    sudo apt-get install libbsd-dev
# build
    mkdir build
    cd build
    cmake ..
    make
#install
## Raspian Wheezy
    sudo make install
    sudo cp ../raspi2raspi.init.d /etc/init.d/raspi2raspi
    sudo update-rc.d raspi2raspi defaults
    sudo service raspi2raspi start
## Raspian Jessie
    sudo make install
    sudo cp ../raspi2raspi.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable raspi2raspi.service
    sudo systemctl start raspi2raspi
#uninstall
## Raspian Wheezy
    sudo service raspi2raspi stop
    sudo update-rc.d -f raspi2raspi remove
    sudo rm /usr/local/bin/raspi2raspi
    sudo rm /etc/init.d/raspi2raspi
## Raspian Jessie
    sudo systemctl stop raspi2raspi
    sudo systemctl disable raspi2raspi.service
    sudo rm /etc/systemd/system/raspi2raspi.service
    sudo rm /usr/local/bin/raspi2raspi
