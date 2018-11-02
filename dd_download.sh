sudo gunzip -c SD/bpi-m2p/100MB/BPI-M2P-720P.img.gz | dd of=$1 bs=1024 seek=8
sync
cd SD/bpi-m2p
sudo bpi-update -d $1
