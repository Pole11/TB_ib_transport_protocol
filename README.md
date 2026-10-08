### Step 0

```bash
source /tools/Xilinx/Vitis_HLS/2022.1/settings64.sh
```

### How to run tests with g++

Optionally clean
```bash
make clean
```
then run
```bash
make run
```

### How to run tests with Vitis compiler

```bash
vitis_hls -f run_csim.tcl
```

### How to see waveform

```bash
vitis_hls -f run_cosim.tcl
```

### How to check for timing constraints

```bash
TARGETS="100G" vitis_hls -f run_csynth.tcl
TARGETS="200G" vitis_hls -f run_csynth.tcl
```

### How to generate project and generate bitstream

```bash
sudo mount -o loop /run/media/pole/minion/xilinx-minion.img /tools/Xilinx-minion
source /tools/Xilinx-minion/Vitis_HLS/2024.2/settings64.sh

cd ../Coyote-mrc/examples/09_perf_rdma/hw
mkdir build_hw && cd build_hw
cmake ../ -DFDEV_NAME=u55c
make project && make bitgen # it takes hours (~20 hours)
```

### Notes

- `src/dut_top.cpp` and `include/tb/dut_top.hpp` are created just because Vitis complains about the name of the DUT, it must not contain angle brackets or any strange character, so a wrapper around `ib_transport_protocol<DATA_WIDTH, 0>` is needed
