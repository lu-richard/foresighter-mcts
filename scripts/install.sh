#!/bin/bash
# this script set up the environment needed for foresighter

PYTHON_COMMAND=python3


function install_basics_mac(){
    # nothing to do here
    tput setaf 2;
    echo "Installing basics for mac..."
}

function install_basics_linux(){
    tput setaf 2;
    echo "Installing basics for linux..."
    apt-get update
    apt install python3 python3-pip cmake python3-venv -y --fix-missing
}

function install_basics(){
    # if it is mac, use brew. If it is linux, use apt-get. Else, exit.
    if [[ "$OSTYPE" == "darwin"* ]]; then
        install_basics_mac
    elif [[ "$OSTYPE" == "linux-gnu" ]]; then
        install_basics_linux
    else
        echo "Unsupported OS"
        exit 1
    fi
}


function install_pandoc_and_weasyprint(){
    # install pandoc

    # if it is mac, use brew. If it is linux, use apt-get. Else, exit.
    if [[ "$OSTYPE" == "darwin"* ]]; then
        brew install pandoc
        brew install weasyprint
    elif [[ "$OSTYPE" == "linux-gnu" ]]; then
        apt-get install -y pandoc
        apt-get install -y weasyprint
    else
        echo "Unsupported OS"
        exit 1
    fi
}

function create_virtual_env_and_install_requirements(){
    # create virtual environment
    $PYTHON_COMMAND -m venv .venv
    # activate virtual environment
    source .venv/bin/activate
    # install requirements
    pip install -r requirements.txt
}


function install_dependencies(){
    # install basics
    install_basics
    # install pandoc and weasyprint
    install_pandoc_and_weasyprint
    # create virtual environment and install requirements
    create_virtual_env_and_install_requirements
}

install_dependencies
# echo green setup complete message
tput setaf 2;
echo "Setup complete."

# try build the project
tput setaf 2;
echo "Trying to build the project..."
sh scripts/run_build_release.sh

tput setaf 2;
echo "Finish setting up."

# set back to normal text color
tput sgr0;
