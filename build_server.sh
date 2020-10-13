PROJ_ROOT=`pwd`

# build server
mkdir -p $PROJ_ROOT/server/build
cd $PROJ_ROOT/server/build && cmake .. && make

#build driver
cd $PROJ_ROOT/driver && make && cd $PROJ_ROOT
