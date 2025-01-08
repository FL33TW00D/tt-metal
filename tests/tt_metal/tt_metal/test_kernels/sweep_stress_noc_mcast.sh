#/bin/bash

function run_set() {
    echo "running: $@"
    TT_METAL_SLOW_DISPATCH_MODE=1 build/test/tt_metal/test_stress_noc_mcast -d 90 -tlx 0 -tly 0 -width 8 -height 8 $@
    echo "running: $@ -v 1"
    TT_METAL_SLOW_DISPATCH_MODE=1 build/test/tt_metal/test_stress_noc_mcast -d 90 -tlx 0 -tly 0 -width 8 -height 8 $@ -v 1
    echo "running: $@ -v 2"
    TT_METAL_SLOW_DISPATCH_MODE=1 build/test/tt_metal/test_stress_noc_mcast -d 90 -tlx 0 -tly 0 -width 8 -height 8 $@ -v 2
    echo "running: $@ -v 3"
    TT_METAL_SLOW_DISPATCH_MODE=1 build/test/tt_metal/test_stress_noc_mcast -d 90 -tlx 0 -tly 0 -width 8 -height 8 $@ -v 3

}

function run_all() {
    run_set $@
    run_set $@ -m 256
    run_set $@ -m 2048
    run_set $@ -u 32
    run_set $@ -u 32 -m 256
    run_set $@ -u 32 -m 2048
}

# sweep w/ randomized delay+noc address
for (( i=0; i<=11; i++ )); do
    run_all -en $i -rdelay -rcoord
done

# sweep w/ right to left transactions
for (( i=0; i<=11; i++ )); do
    run_all -en $i
done

# sweep w/ bottom to top transfers
for (( i=0; i<=11; i++ )); do
    run_all -en $i -l
done
