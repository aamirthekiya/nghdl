#!/bin/bash 
#===============================================================================
#
#          FILE: install-nghdl.sh
# 
#         USAGE: ./install-nghdl.sh --install
#                 or
#                ./install-nghdl.sh --uninstall
# 
#   DESCRIPTION: It is installation script for ngspice and ghdl work (nghdl). 
# 
#       OPTIONS: ---
#  REQUIREMENTS: ---
#          BUGS: ---
#         NOTES: ---
#        AUTHOR: Fahim Khan, Rahul Paknikar
#  ORGANIZATION: eSim, FOSSEE group at IIT Bombay
#       CREATED: Wednesday 26 November 2019 14:30
#      REVISION:  ---
#===============================================================================

ngspice="ngspice-nghdl"
ghdl="ghdl-0.36"
config_dir="$HOME/.nghdl"
config_file="config.ini"
src_dir=`pwd`

#Will be used to take backup of any file
sysdate="$(date)"
timestamp=`echo $sysdate|awk '{print $3"_"$2"_"$6"_"$4 }'`


#All functions goes here
function installDependency
{

    echo "Updating indexes to install latest versions......"
    sudo apt-get update
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe APT repository couldn't update.\nKindly resolve above APT repository errors and try again."
        exit 1
    fi

    echo "Installing dependencies for ghdl-0.36 LLVM......."
    echo "Installing make.................................."
    sudo apt-get install -y make
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe make dependency couldn't be install.\nKindly resolve above APT repository errors and try again."
        exit 1
    fi
    echo "Installing gnat-5................................"
    sudo apt-get install -y gnat-5
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe gnat-5 dependency couldn't be install.\nKindly resolve above APT repository errors and try again."
        exit 1
    fi
    echo "Installing llvm.................................."
    sudo apt-get install -y llvm
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe llvm dependency couldn't be install.\nKindly resolve above APT repository errors and try again."
        exit 1
    fi
    echo "Installing clang................................."
    sudo apt-get install -y clang
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe clang dependency couldn't be install.\nKindly resolve above APT repository errors and try again."
        exit 1
    fi
    echo "Installing zlib1g-dev............................"
    sudo apt-get install -y zlib1g-dev
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe zlib1g-dev dependency couldn't be install.\nKindly resolve above APT repository errors and try again"
        exit 1
    fi
    echo "Installing xterm................................."
    sudo apt-get install -y xterm
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe xterm dependency couldn't be install.\nKindly resolve above APT repository errors and try again."
        exit 1
    fi

    echo "Installing ghdl.................................."
    grep -h "ghdl" /usr/local/bin/ghdl > /dev/null
    if [ $? -ne 0 ]; then
        tar -xzvf ghdl-0.36.tar.gz
        if [ "$?" == 0 ]; then
            echo "ghdl-0.36 LLVM successfully extracted to $HOME......"
            echo "Changing directory to ghdl-0.36 LLVM installation..."
            cd ghdl-0.36/
            echo "Configuring ghdl-0.36 build as per requirements....."
            #Other configure flags can be found at - https://github.com/ghdl/ghdl/blob/master/configure
            sudo ./configure --with-llvm-config
            echo "Building the install file for ghdl-0.36 LLVM....."
            sudo make
            echo "Installing ghdl-0.36 LLVM....."
            sudo make install
            echo "Removing unused part of ghdl-0.36 LLVM....."
    		sudo rm -rf ../ghdl-0.36
        else
            echo "Unable to extract ghdl-0.36 LLVM"
            echo "Exiting installation"
            exit 1
        fi
    else
        echo "ghdl already exists....."
        echo "Leaving ghdl-0.36 LLVM installation"
    fi
    
    echo "Installing flex.................................."
    sudo apt-get install -y flex
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe flex dependency couldn't install.\nKindly resolve above APT repository error and try again."
        exit 1
    fi
    echo "Installing bison................................."
    sudo apt-get install -y bison
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe bison dependency couldn't be install.\nKindly resolve above APT repository error and try again."
        exit 1
    fi

    # Specific dependency for nvidia graphic cards
    echo "Installing graphics dependency for ngspice souce build"
    echo "Installing libxaw7................................"
    sudo apt-get install libxaw7
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe libxaw7 graphics dependency couldn't be install.\nKindly resolve above APT repository error and try again."
        exit 1
    fi
    echo "Installing libxaw7-dev............................"
    sudo apt-get install libxaw7-dev
    if [ $? -ne 0 ]; then
        echo -e "\n\n\nThe libxaw7-dev graphics dependency couldn't be install.\nKindly resolve above APT repository error and try again."
        exit 1
    fi
}


function installNgspice
{
    echo "Installing ngspice................................"
    #Checking if ngspice-nghdl directory is already present in Home directory
    if [ -d $HOME/$ngspice ];then
        echo "$ngspice directory already exists at $HOME"
        echo "Leaving ngspice installation.................."
    else
        #Extracting Ngspice to Home Directory
        cd $src_dir
        tar -xzvf $ngspice.tar.gz -C $HOME 
        if [ "$?" == 0 ];then 
            echo "Ngspice extracted sucessfuly to $HOME "
            #change to ngspice-nghdl directory
            cd $HOME/$ngspice
            #Make local install directory
            mkdir -p install_dir
            #Make release directory for build
            mkdir -p release
            #Change to release directory
            cd release
            echo "Installing Ngspice....."
            echo "-----------------------------------------"  
            sleep 5
            ../configure --enable-xspice --disable-debug  --prefix=$HOME/$ngspice/install_dir/ --exec-prefix=$HOME/$ngspice/install_dir/
            
            #dirty fix for adding patch to ngspice base code
            cp $src_dir/src/outitf.c $HOME/$ngspice/src/frontend
 
            make
            make install
            if [ "$?" == 0 ];then
                echo "Removing previously Installed Ngspice (if any)......"
                sudo apt-get remove -y ngspice

                echo "Ngspice Installed sucessfully......"
                echo "Adding softlink for the installed ngspice......"

                sudo ln -s $HOME/$ngspice/install_dir/bin/ngspice /usr/bin/ngspice
                if [ $? -ne 0 ];then
                    echo "Failed to add ngspice softlink"
                    echo "Remove earlier installations at /usr/bin/ngspice and try again..."
                    exit 1
                else
                    echo "Added softlink for ngspice"
                fi

            else 
                echo "There was some error in installing ngspice"
            fi

        else 
            echo "Unable to extract ngspice tar file"
            exit 1;
        fi
    fi

}

function createConfigFile
{
    
    #Creating config.ini file and adding configuration information
    #Check if config file is present
    if [ -d $config_dir ];then
        rm $config_dir/$config_file && touch $config_dir/$config_file
    else
        mkdir $config_dir && touch $config_dir/$config_file
    fi
    
    echo "[NGSPICE]" >> $config_dir/$config_file
    echo "NGSPICE_HOME = $HOME/$ngspice" >> $config_dir/$config_file
    echo "DIGITAL_MODEL = %(NGSPICE_HOME)s/src/xspice/icm/ghdl" >> $config_dir/$config_file
    echo "RELEASE = %(NGSPICE_HOME)s/release" >> $config_dir/$config_file
    echo "[SRC]" >> $config_dir/$config_file
    echo "SRC_HOME = $src_dir" >> $config_dir/$config_file
    echo "LICENSE = %(SRC_HOME)s/LICENSE" >> $config_dir/$config_file

}

function createSoftLink
{
    
    ## Creating softlink 
    cd /usr/local/bin
    if [[ -L nghdl ]];then
        echo "Symlink was already present"
        sudo unlink nghdl
        sudo ln -sf $src_dir/src/ngspice_ghdl.py nghdl
    else
        echo "Creating symlink"
        sudo ln -sf $src_dir/src/ngspice_ghdl.py nghdl
        if [ $? -ne 0 ];then
            echo "Failed to add nghdl softlink"
            echo "Remove earlier installations at /usr/local/bin/nghdl and try again..."
            exit 1
        else
            echo "Added softlink for nghdl......"
        fi
    fi
    cd $pwd

}

#####################################################################
#       Script start from here                                     #
#####################################################################

###Checking if file is passsed as argument to script

if [ "$#" -eq 1 ];then
    option=$1
else
    echo "USAGE : "
    echo "./install-nghdl.sh --install"
    exit 1;
fi

##Checking flags
if [ $option == "--install" ];then
    
    #Calling functions
    installDependency
    if [ $? -ne 0 ];then
        echo -e "\n\n\nERROR: Unable to install required packages. Please check your internet connection.\n\n"
        exit 0
    fi
    installNgspice
    createConfigFile
    createSoftLink

elif [ $option == "--uninstall" ];then
    echo "Deleting Files............"
    sudo rm -rf $HOME/ngspice-nghdl $HOME/.nghdl /usr/share/kicad/library/eSim_Nghdl.lib /usr/local/bin/nghdl /usr/bin/ngspice
    echo "Removing gnat-5............"
    sudo apt-get remove -y gnat-5
    echo "Removing llvm............"
    sudo apt-get remove -y llvm
    echo "Removing ghdl............"
    sudo rm -rf /usr/local/bin/ghdl /usr/local/lib/ghdl /usr/local/bin/ghdl1-llvm /usr/local/lib/libghdlvpi.so

else 
    echo "Please select the proper operation."
    echo "--install"
    echo "--uninstall"
fi
