if [[ $EUID -ne 0 ]]; then
    echo "The server installer must be run as root"
    exit 1
fi

g++ server/src/*.cpp shared/src/*.cpp -lpthread -std=c++17 -O3 -o server/remramd_server

cd driver && make && insmod ramd.ko && cd ..
