PROJ_ROOT=`pwd`

# build client
mkdir -p $PROJ_ROOT/client/build
cd $PROJ_ROOT/client/build && cmake .. && make
