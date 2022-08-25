#source /opt/imx6ul/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi
source /opt/6ul-5.10sdk/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi
make distclean

make ARCH=arm  myd_y6ulx_defconfig
#make ARCH=arm O="$PWD/../build" mys_6ulx_defconfig
#make mys_6ulx_defconfig
make ARCH=arm zImage dtbs   -j16
#make  modules  -j16


#make modules_install O="$PWD/../build"
mkdir -p $PWD/../build
cp $PWD/arch/arm/boot/zImage $PWD/../build/
cp $PWD/arch/arm/boot/dts/myd-y6ull*.dtb $PWD/../build/
cp $PWD/arch/arm/boot/dts/myd-y6ul*.dtb $PWD/../build/
