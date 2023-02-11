
#######INSTALL JARVIS-CD

#######INSTALL SCSPKG
cd "${HOME}" || exit
git clone https://github.com/scs-lab/scspkg.git
cd scspkg || exit
bash install.sh
source ~/.bashrc

########INSTALL libmemcached
scspkg create libmemcached
cd $(scspkg pkg-src libmemcached) || exit
wget https://launchpad.net/libmemcached/1.0/1.0.18/+download/libmemcached-1.0.18.tar.gz
tar -xzf libmemcached-1.0.18.tar.gz
cd libmemcached-1.0.18 || exit
./configure --prefix=$(scspkg pkg-root libmemcached)

###There's an error which prevents it from compiling, which I guess they didn't patch.
cat << EOF > memflush.patch
opt_servers is declated as 'static char *opt_servers= NULL;'
diff --git a/clients/memflush.cc b/clients/memflush.cc
index 8bd0dbf..7641b88 100644
--- clients/memflush.cc
+++ clients/memflush.cc
@@ -39,7 +39,7 @@ int main(int argc, char *argv[])
 {
   options_parse(argc, argv);

-  if (opt_servers == false)
+  if (!opt_servers)
   {
     char *temp;

@@ -48,7 +48,7 @@ int main(int argc, char *argv[])
       opt_servers= strdup(temp);
     }

-    if (opt_servers == false)
+    if (!opt_servers)
     {
       std::cerr << "No Servers provided" << std::endl;
       exit(EXIT_FAILURE);
EOF
patch clients/memflush.cc memflush.patch

make -j8
make install
module load libmemcached

###########INSTALL MPICH
spack install mpich
spack load mpich

###########INSTALL PROTOBUF
spack install protobuf-c
spack load protobuf-c

###########INSTALL NATS
scspkg create cnats
cd $(scspkg pkg-src cnats) || exit
wget https://github.com/nats-io/nats.c/archive/refs/tags/v3.3.0.tar.gz
tar -xzf v3.3.0.tar.gz
cd nats.c-3.3.0 || exit
./install_deps.sh
mkdir build
cd build || exit
cmake ../ -DCMAKE_INSTALL_PREFIX=$(scspkg pkg-root cnats)
make -j8
make install

############INSTALL CEREAL
spack install cereal
spack load cereal

spack load libmemcached nats-c yaml-cpp
module load libmemcached-1.0.18-gcc-9.3.0-ugz2lwc
module load mpich-3.3.2-gcc-9.3.0-mrb5naa
module load protobuf-c-1.3.2-gcc-9.3.0-fmmtrx4
module load nats-c-3.3.0-gcc-9.3.0-myawpzv
module load cereal-1.3.0-gcc-9.3.0-ww6zqh4
module load cityhash-2013-07-31-gcc-9.3.0-dbbe6o7
#module load yaml-cpp-0.7.0-gcc-9.3.0-mxs6yil
module load yaml-cpp-0.6.3-gcc-9.3.0-kbtrofj
