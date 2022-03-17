# Build instructions

## Clone and uild vpp release

```bash
git clone https://git.fd.io/vpp
cd vpp
make install-deps
make build-release
cd ..

```

## Clone and build vpp-toys

``` bash
git clone https://github.com/sheercoding/testcases_for_vpp.git
cd testcases_for_vpp 
make build
```
binaries can be found in `./bin`
