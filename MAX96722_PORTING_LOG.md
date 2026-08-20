# MYD-JS8MPQ MAX96722 Porting Log

## Scope and baselines

- Date/timezone: 2026-08-14, Asia/Shanghai
- Board/SOC/variant: MYIR MYD-JS8MPQ, i.MX8MP, 2 GiB, native HDMI + physical CSI1
- Reference topology: NXP `imx8qm-mek-max9286-csi1.dtso`, adapted to the i.MX8MP CSI receiver graph
- Linux target: `develop_6.12.49`, working-tree baseline `87b574b4cf10`
- Running board kernel: `6.12.49-g5434f1edadb3-dirty`
- Running DT model: `MYIR MYD-JS8MPQ board (HDMI + CSI1 MAX96722)`
- Toolchain: Yocto `aarch64-poky-linux-gcc 14.3.0`
- Yocto kernel work directory:
  `yocto/build-xwayland/tmp/work/myd_js8mpq-poky-linux/linux-imx/6.12.49+git`

## Hardware mapping

- Physical connection: MAX96722 on the board CSI1 connector
- Linux I2C bus: `/dev/i2c-2`
- Device-tree controller: `&i2c3`, `30a40000.i2c`
- MAX96722 address: `0x6b`
- Device ID register `0x000d`: `0xa1`
- Device revision register `0x004c`: `0x05`
- CSI graph: `MAX96722 -> mipi_csi_0@32e40000 -> isi_0@32e00000`
- Current population: four camera modules connected; each camera uses a
  MAX96717 serializer (camera sensor type is still unknown)

The Linux I2C adapter number is runtime numbering and does not equal the DTS
label suffix. On this image, `i2cdetect -y -r 2` is the correct bus for
`&i2c3`.

## Adaptation plan and risks

1. Reuse the NXP MAX96724 driver because MAX96722 uses the same 16-bit register
   access model required by the functions used during bring-up.
2. Add the MAX96722 device ID and OF compatible without removing MAX96724
   support.
3. Add an opt-in no-link mode for deserializer-only bring-up. Normal nodes that
   omit the property keep the original probe failure behavior.
4. Keep the change board-scoped with a separate HDMI + CSI1 DTB.
5. Disable all remote I2C mux buses until the real serializer and sensor types,
   aliases, link rate, virtual channels, and pixel format are known.

The RDACM20 remote nodes currently present in the DTS are disabled reference
placeholders. They must not be enabled blindly when a different camera module
is connected.

## Functional record

### MAX96722 driver identification and no-link mode

- Source basis: the NXP MAX96724 driver was already present in the target
  baseline; this change adapts it for the MAX96722 hardware.
- Target files:
  - `drivers/media/i2c/max96724.c`
  - `drivers/media/i2c/Kconfig`
  - `Documentation/devicetree/bindings/media/i2c/maxim,max96724.yaml`
- Adaptation:
  - accept device ID `0xa1` in addition to MAX96724 ID `0xa2`;
  - add `maxim,max96722` matching;
  - add opt-in `maxim,allow-no-link` handling;
  - report the detected chip and configured/locked link masks.
- Intentional exclusion: no serializer, sensor, link-rate, VC, or CSI data-type
  programming was added without the real camera hardware definition.
- Static check: `git diff --check` passed for the MAX96722 files.
- Module build: external module build passed with `W=1`, Yocto GCC 14.3, and
  the Yocto `build/Module.symvers`.
- Module artifact: `/tmp/max96722-ext.ApC6c2/max96724.ko`, SHA-256
  `73460de5732f12aaf1c540629a751bc9bdb834d272c4e4b09104c98fa2856582`.
- Module vermagic:
  `6.12.49-g5434f1edadb3-dirty SMP preempt mod_unload modversions aarch64`.
- Board installation:
  `/lib/modules/6.12.49-g5434f1edadb3-dirty/extra/max96724.ko`.
- Board rollback copy:
  `/lib/modules/6.12.49-g5434f1edadb3-dirty/extra/max96724.ko.pre-max96722`.

### HDMI + CSI1 device-tree variant

- Target files:
  - `arch/arm64/boot/dts/myir/myd-js8mpq-hdmi-max96722-2g.dts`
  - `arch/arm64/boot/dts/myir/Makefile`
- Adaptation:
  - preserve native HDMI and disable the inherited LVDS path;
  - place `deserializer@6b` below `&i2c3`;
  - connect the MAX96722 four-lane output to `mipi_csi_0` and `isi_0`;
  - disable the unused `mipi_csi_1`/`isi_1` path;
  - enable all four remote mux channels and add one `serializer@40` MAX96717
    node per link;
  - connect each MAX96717 GMSL output endpoint to the matching MAX96722 sink;
  - leave the MAX96717 CSI input endpoints unconnected until the real sensor
    type and address are known.
- DTB artifact:
  `arch/arm64/boot/dts/myir/myd-js8mpq-hdmi-max96722-2g.dtb`, 65549 bytes,
  SHA-256
  `89d365bc135064d5ea6ae711acfc4333cec89d2adcf2a8630586528d4d92dc23`.
- DTB read-back confirmed `i2c@30a40000/deserializer@6b`,
  `compatible = "maxim,max96722"`, `maxim,allow-no-link`, `mipi_csi_0`, and
  `isi_0`, plus four `maxim,max96717` serializer nodes.

### MAX96717 serializer driver and topology

- Target files:
  - `arch/arm64/configs/myd-js8mpq_defconfig`
  - `drivers/media/i2c/max96717.c`
  - `arch/arm64/boot/dts/myir/myd-js8mpq-hdmi-max96722-2g.dts`
- Configuration: `CONFIG_VIDEO_MAX96717=m`; this is the YAML-backed V4L2
  driver and is distinct from `CONFIG_VIDEO_MAX96717_LIB`.
- Kconfig validation: standalone `conf` generation from
  `myd-js8mpq_defconfig` produced `CONFIG_VIDEO_MAX96717=m`.
- Driver adaptation: permit parsing an unconnected CSI sink endpoint during
  serializer-only bring-up and report a detected MAX96717 ID/revision at info
  level.
- Module build: external `W=1` build passed with Yocto GCC 14.3 and the Yocto
  `Module.symvers`.
- Module artifact: `/tmp/max96717-230.4YHc6O/max96717.ko`, SHA-256
  `3ffa2ee516be4b5abf4e9d498d9501ccb3f06e9484183339ccee62d9878e84bc`.
- Module vermagic:
  `6.12.49-lts-next-gdc3c935e12a5 SMP preempt mod_unload modversions aarch64`.
- Board installation:
  `/lib/modules/6.12.49-lts-next-gdc3c935e12a5/updates/max96717.ko`.
- DTB installation:
  `/run/media/mmcblk2p1/myd-js8mpq-hdmi-max96722-2g.dtb`.
- DTB rollback copy:
  `/run/media/mmcblk2p1/myd-js8mpq-hdmi-max96722-2g.dtb.pre-max96717-20260818`.
- Live probe: all four instances progressed through endpoint parsing and
  attempted DEV_ID register `0x000d`; all returned `-ENXIO` because no real
  GMSL lock/reverse control channel exists yet.
- `dtbs_check` could not run because the host lacks `dt-doc-validate`; DTC,
  DTB read-back, graph symmetry, and live OF-node checks passed.

## ABI diagnosis

The first deployed module had matching vermagic but was compiled against stale
GCC 11 generated data and symbol CRCs. The running kernel was built by Yocto
GCC 14.3 with `CONFIG_MODVERSIONS=y`, so the module failed with CRC errors for
`_dev_info`, `_dev_err`, and `_dev_warn`.

The corrected module reused the Yocto kernel `build/Module.symvers` and GCC
14.3. The board and Yocto configurations differ only in the built-in/module
choice for OV13855 and OV5640; their module ABI settings match. The Yocto
generated release files were temporarily aligned during the external module
build and restored immediately afterward.

## Hardware validation

| Function | Result | Evidence |
|---|---|---|
| DTB boot | Passed | Board model reports HDMI + CSI1 MAX96722 |
| I2C presence | Passed | `i2cdetect -y -r 2` shows `0x6b`; after bind it shows `UU` |
| Driver ABI | Passed | Module auto-loads after reboot with no unknown-symbol or CRC errors |
| MAX96717 module ABI | Passed | `max96717` and dependency `v4l2_cci` load without CRC or unknown-symbol errors |
| Chip identification | Passed | `Detected MAX96722 deserializer, id 0xa1` |
| No-link bring-up | Passed | `No GMSL link locked; continuing in no-link debug mode` |
| CSI1 receiver | Passed | `32e40000.csi`: 4 lanes, HS settle 10, clock settle 2 |
| ISI0 | Passed | `32e00000.isi: mxc_isi.0 registered successfully` |
| GMSL lock | Failed | Four MAX96717 camera modules connected, but the real A-D LOCK bits remain clear |
| MAX96717 DT instances | Passed | Four devices exist at `7-0040` through `10-0040` and the module auto-loads |
| MAX96717 DEV_ID read | Failed | Driver reads register `0x000d` on all four buses and receives `-ENXIO` |
| Media/video node | Not expected yet | MAX96717 CSI sink endpoints have no real sensor subdevice yet |
| Frame capture | Not tested | Requires serializer and camera hardware/configuration |

### MAX96717 link-rate diagnosis

- The in-tree MAX96717 driver defines default address `0x40` and DEV_ID
  register `0x000d`. It accepts `0xbf` for MAX96717 and `0xc8` for
  MAX96717F. The older NXP helper library contains a conflicting MAX96717
  value of `0xb7`, so the actual value must be recorded once the link is
  reachable.
- MAX96717 supports 3Gbps or 6Gbps forward rate; MAX96717F supports only
  3Gbps.

- The normal MAX96722 state is four GMSL2 links enabled with
  `REG6=0xff`, `REG10=0x11`, and `REG11=0x11` (3Gbps RX on A-D).
- A link-A-only 6Gbps test wrote `REG10=0x12`. The A RX-rate field read
  back as zero (`REG10=0x10`), both while link A was enabled and when the
  rate was written while link A was disabled before re-enabling it.
- Therefore 6Gbps was not actually selected and cannot be counted as a
  completed 6Gbps link test. In both attempts the real link-A status was
  `REG0x001a=0x12`: CMU lock set, full link lock clear. MAX96717 at `0x40`
  still did not acknowledge register `0x000d`.
- After testing, the four-link 3Gbps register state was restored and the
  `max96724` driver was rebound successfully.

## 2026-08-18 live validation update

This section supersedes the earlier no-lock/DEV_ID conclusions above; those
entries are retained only as bring-up history.

- Running kernel: `6.12.49-lts-next-gdc3c935e12a5`.
- Boot selection: extlinux label `max96722`, DTB
  `/myd-js8mpq-hdmi-max96722-2g.dtb`.
- All four GMSL links lock and all four MAX96717 instances bind.  Each reports
  device ID `0xbf`, revision 6, on mux buses 7 through 10.
- The MAX96724-compatible driver now selects MAX96717 video pipe Z, provides
  legacy `s_power`/`s_stream` callbacks, programs the port-A lane-map register,
  and accepts both RGB888 and BGR888 media-bus codes for CSI-2 data type
  `0x24`.
- The MAX96717 test-pattern path supplies explicit 24-bpp, VC0, DT `0x24`
  metadata.  Link A reaches video lock and MAX96722 CSI TX/PHY packet counters
  increment while streaming.
- Deployed artifacts:
  - `max96717.ko`: SHA-256
    `888b9f0942e5229f5669614572061eeff1370a3056ba8135d32f4712c075ca58`;
  - `max96724.ko`: SHA-256
    `16da7e1e85f83d68165e9c4bbd28eed3fdd72ada6a2fd95e560fcb7000151db0`;
  - DTB (`csis-hs-settle = <16>`): SHA-256
    `ef5820f0639d9db6ee01a558cbab9f95d9dea494545256925256255c02cff6a8`.
- Link-A checkerboard capture is still not functional: `/dev/video0` times
  out with a zero-byte file.  With the stream active, MAX96722 reports video
  lock and CSI packet counts, while i.MX8MP CSI1 reports `DPHYSTATUS=0x22`,
  zero IRQs, zero frame/SOT/error events.  HS settle 10 versus 16, all 64
  port-A polarity values, and all 256 lane-map values produced no CSI IRQ.
- The schematic shows direct P-to-P/N-to-N DA0-DA3/CKA routing.  NCA9555 P05
  and P06 control resistors are DNP; the 1.8-V and 1.2-V rails are enabled by
  hardware straps, so no additional GPIO hog is required for those rails.
- The running built-in CSI driver can still warn in the legacy
  `get_frame_interval` path.  Source now obtains the remote subdevice's active
  state for set/get-frame-interval calls and builds successfully with `W=1`,
  but this fix requires a future Image replacement.
- Safe idle state after testing: all test patterns disabled, MAX96717 tunnel
  register `0x0383=0x80` on all four links, MAX96722 lane map `0x08a3=0xe4`,
  polarity `0x08a5=0x00`, and CSI debug disabled.

## 2026-08-19 SC360AT final hardware validation

This section supersedes the 2026-08-18 zero-frame and electrical-debug
conclusions.  The production camera path is now functional without manual I2C
initialization:

`SC360AT + ISP -> MAX96717G -> MAX96722 Link A -> i.MX8MP CSI1 (2 lanes)
-> ISI1 -> /dev/video0`.

- Target address during the final recheck: `192.168.40.244`.
- Running kernel: `6.12.49-lts-next-gdc3c935e12a5`.
- Active model: `MYIR MYD-JS8MPQ board (HDMI + CSI1 MAX96722)`.
- Active topology exposes `/dev/video0` at 1920x1536 YUYV, with a
  5,898,240-byte image size and Link A as the only active GMSL input.
- A three-frame mmap capture produced exactly 17,694,720 bytes.  A subsequent
  30-frame stop/start capture completed at 30.03 fps, and a third 90-frame
  capture also completed at 30.03 fps.
- During the 90-frame capture, MAX96717 register `0x0112` read `0x8a`
  (`PCLKDET=1`) and MAX96722 register `0x0108` read `0xf2`
  (`VID_PKT_DET=1`, `VID_LOCK=1`).
- MAX96722 MIPI error-packet registers `0x08e1` through `0x08e4` all read
  `0x00`; the kernel log contained no new CSI overflow or error-packet event.
- The frame copied from the target is stored as
  `/media/disk_p/wujl/myd-js8mp/sc360at-192.168.40.244-recheck-3frames.raw`
  (SHA-256 `8de27d1647af5ae0c451b1bd83948a0eb952da45c3094739002ff6a5b47951b0`).
- Deployed module hashes are `02fb46f1...c194` for `max96717.ko`,
  `e96f5af5...efeaa6` for the active updates copy of `max96724.ko`, and
  `179d6d4b...bb50` for `imx8-media-dev.ko`.
- The boot-partition DTB is
  `/run/media/mmcblk2p1/myd-js8mpq-hdmi-max96722-2g.dtb`, SHA-256
  `c77fea53948c4bb9645bffa623c03697f38ffc5b5e401042d49ce1590150bace`.

## Residual scope

- Only the requested Link A SC360AT camera was validated.  Links B-D and
  multi-VC capture are intentionally disabled and are not claimed as tested.
- The serializer still prints `No sensor endpoint; test-pattern mode only`
  because the module's ISP-to-serializer pixel input has no separate Linux
  sensor subdevice.  This message is cosmetic for the ALI360 profile: the
  captured frame is the real external camera image, not the MAX96717 pattern
  generator.
- `fw_printenv fdtfile` retains the board's generic default filename, while the
  extlinux camera entry loads the MAX96722 DTB above.  The active model and
  media graph confirm that the camera DTB was used.

## 2026-08-19 ALI360 exposure-convergence fix

The vendor PHY profile removed the visible line corruption, but the first
buffers after every module reset were still nearly black.  A controlled test
with the same deployed register state measured the following mean Y values:

- no skipped frames: `15.487`;
- 30 skipped frames: `110.654`;
- 150 skipped frames: `90.057`;
- 300 skipped frames: `89.834`.

This isolates the dark image to the SC360AT ISP automatic-exposure convergence
window, not the MAX96722 PHY settings or YUV byte order.  The ALI360 profile now
keeps CSI output disabled for 1000 ms after `VID_PKT_DET` and `VID_LOCK`, while
the internal 30 Hz FSYNC continues to run.  The delay is board-profile scoped
and does not change generic MAX96722/MAX96724 behavior.

After a reboot with the new module, three independent no-skip, three-frame
captures produced per-frame mean Y values of `[90.849, 91.068, 89.064]`,
`[93.104, 90.888, 91.301]`, and `[93.131, 90.904, 91.302]`.  Each output was
exactly 17,694,720 bytes.  A no-query 300-frame run completed in 12 seconds at
30.03 fps with no visible horizontal corruption or bottom green band.

Register read-back during streaming confirmed that the validated vendor profile
was preserved:

- two-lane map `0x08a3=0x44`;
- DPLL `0x0415/0x0418/0x041b/0x041e=0x2a`;
- charge pump `0x08a9=0xc8`, `0x08aa=0xe0`;
- FSYNC period `0x0cb735` (833,333 crystal clocks, 30 Hz), FSYNC error count 0;
- `PCLKDET=1`, `VID_PKT_DET=1`, `VID_LOCK=1`;
- MAX96722 MIPI error-packet counters `0x08e1-0x08e4=0`.

The deployed `max96724.ko` SHA-256 is
`7f82eefa1cb7ec6f7044b3d9e917fc248bb21b14724da87efd0fe68e2d578fd7`.
The previous module remains recoverable as
`max96724.ko.pre-ali360-ae-20260819` on the target.

The i.MX8MP CSIS diagnostic counters still grow during a long stream (26 ECC
and 462,889 SOT events over the final 300-frame diagnostic run), despite clean
captured images and zero MAX96722 error-packet counters.  Treat this as a
receiver-side timing/diagnostic residual rather than claiming an error-free
electrical link.  Reading subdevice status repeatedly during an active stream
also disturbed that stream; post-stream status reads were used for the final
300-frame test.

## 2026-08-19 N4-style four-camera switching

The 6.18 BSP's NVP6324/N4 implementation initializes four inputs but exposes
one VC0 capture stream selected by `jaguar1_active_ch`.  The MAX96722 port now
uses the same observable model:

- all four MAX96717/SC360AT modules bind and are started when `/dev/video0`
  starts streaming;
- exactly one MAX96722 video pipe and its three image/FS/FE mappings are
  enabled at a time;
- every selected pipe is remapped to VC0 and captured through
  `mipi_csi_1 -> isi_1 -> /dev/video0`;
- `/sys/bus/i2c/devices/2-006b/active_camera` selects channels 0 through 3,
  including while the capture stream remains active;
- the four-pipe wait bug was corrected: only the enabled activity pipe can
  assert `VID_PKT_DET|VID_LOCK`, so STREAMON now waits for that pipe rather
  than waiting for four mutually exclusive pipe locks.

The vendor four-lane register profile was tested before selecting the stable
physical mode.  Register read-back matched the supplied table
(`0x08a3/0x08a4=0xe4`, DPLL `0x2a`, charge pump `0xc8/0xe0`, and DPHY
destination `0x15`), but a 110-frame test produced 84,720 CSIS ECC events,
lost every Frame End, and delivered no capture buffers.  GPU/VPU display
acceleration cannot repair those corrupt CSI-2 packet headers.

The deployed profile therefore keeps the N4 four-camera switching model while
using this board's previously validated two-lane electrical path:

- MAX96722 output lanes `<1 2>`, i.MX8MP legacy receiver lane count `<2>`;
- `csis-hs-settle = <0>` and no `virtual-channel` property;
- lane map `0x08a3=0x44`;
- DPLL `0x0415/0x0418/0x041b/0x041e=0x2a`;
- charge pump `0x08a9=0xc8`, `0x08aa=0xe0`.

Deployed artifacts on target `192.168.40.244`:

- DTB SHA-256:
  `c39aa3d74bd1e6287d3c6107b31b6e03a23d0570982d0ec23c2a3ec3f84811ef`;
- `max96724.ko` SHA-256:
  `babb5aad45cf228db2b298644e8a1402d03b9952c7d6a8330e9532c245c995b8`;
- module vermagic:
  `6.12.49-lts-next-gdc3c935e12a5 SMP preempt mod_unload modversions aarch64`;
- all symbol-version CRCs shared with the board's prior module matched.

Hardware validation:

- boot log: `lanes: 2`, GMSL `configured = 0xf, locked = 0xf`, and four
  MAX96717 devices at buses 7 through 10;
- channels 0/1/2/3 each completed 90 frames at
  30.01/30.03/30.03/30.03 fps in 1920x1080 NV12;
- one continuous 300-frame stream survived `0 -> 1 -> 2 -> 3 -> 0`; the first
  switch took 284 ms and later switches took about 14 ms, with 300 Frame End
  events and a final measured rate of 29.76 fps;
- four 1920x1080 JPEG captures are in
  `/media/disk_p/wujl/myd-js8mp/new_bsp/camera-test-output/`; visual inspection
  found no horizontal bad line or bottom green band;
- a 12-second Weston test negotiated direct NV12 1920x1080@30 from `v4l2src`
  to `waylandsink` using DMABUF and no software color conversion or scaling;
- transient service `max96722-preview-n4.service` was left active on the
  target, displaying channel 0.

The i.MX8MP CSIS SOT diagnostic count remains high in two-lane mode, and a
300-frame live-switch test recorded 56 ECC and 9 lost-Frame-End events even
though all requested buffers completed and all four JPEGs were clean.  This is
an explicit signal-integrity/timing residual; the result is a stable preview
profile, not a claim of an error-free physical link.

## 2026-08-19 preview-stall recovery and final RX4/TX1 switching

The Weston preview later stopped updating even though the GStreamer service
remained active.  The process was sleeping in `poll()`, all four GMSL links
were locked, and MAX96717 `PCLKDET` was set on every link.  The failure was in
the i.MX8MP receiver:

- ISI frame interrupts stopped completely;
- the CSI interrupt rate rose to about 500,000 interrupts/second;
- CSIS `INTSRC=0x10` showed a latched FIFO overflow after sustained SOT
  errors;
- restarting the stream recovered frames only temporarily.

The board DT had `csis-hs-settle = <0>`.  A runtime receiver test with
HS-settle 10 changed a representative 10-second interval from roughly 460,000
CSI error interrupts to 602 normal frame-start/frame-end interrupts while ISI
delivered 300 frames.  `INTSRC` remained zero.  The board DTS now fixes
`csis-hs-settle = <10>`; the resulting D-PHY control register is
`0x0a800007`.

Cold-start testing also exposed why links B-D sometimes timed out after the
earlier single-pipe change.  MAX96722 has two distinct pipe-enable layers:

- `DEV_REG4=0x0f` keeps all four receive packet detectors locked;
- `VIDEO_PIPE_EN` at `0x00f4` selects the single transmit pipe;
- only the selected pipe's three UYVY/frame-start/frame-end mappings are
  enabled and it is still remapped to VC0.

At first STREAMON, all populated receive/transmit pipes are enabled while the
MAX96722 CSI PHY is still off, and all four `VID_PKT_DET|VID_LOCK` states are
waited.  The driver then leaves all receive detectors enabled but reduces the
transmit pipe and mappings to the selected camera.  A live camera change waits
for the prelocked target, changes only the transmit pipe/mapping, and restarts
the MAX96722 CSI PHY for 50 ms so the i.MX8MP CSIS sees a clean packet
boundary.  This avoids both failure modes seen during bring-up: disabling all
receive pipes loses sequence lock, while leaving all transmit pipes enabled
causes duplicated DMA events and CSI errors.

Deployed artifacts on `192.168.40.244`:

- DTB SHA-256:
  `b829c736eaec083aa1fc7dfbe9912afc574175d19285672ed1dd01f8a2c807b2`;
- `max96724.ko` SHA-256:
  `2441f1c8568ccb15de71a0a200693d44a5ac071277320e8ec2e8fc8d399b1317`;
- module vermagic:
  `6.12.49-lts-next-gdc3c935e12a5 SMP preempt mod_unload modversions aarch64`;
- all 80 shared-symbol CRC entries exactly match the previously running
  module.

Final cold-boot hardware validation:

- initial state: `DEV_REG4=0x0f`, `VIDEO_PIPE_EN=0x01`, four receive video
  statuses all contained `VID_PKT_DET|VID_LOCK`, and only pipe 0 had mapping
  mask `0x07`;
- two complete `0 -> 1 -> 2 -> 3 -> 0` live-switch cycles succeeded;
- all nine 5-second windows delivered 149-151 ISI frames, or
  29.8-30.2 fps, with CSIS `INTSRC=0`;
- an additional 120-second channel-0 soak delivered 901-902 frames in every
  30-second interval, or 30.03-30.07 fps, with `INTSRC=0` throughout;
- `max96722-preview-n4.service` was left active on camera 0 using direct
  NV12/DMABUF to `waylandsink`.

Rollback files on the boot/root filesystems include:

- `/run/media/mmcblk2p1/myd-js8mpq-hdmi-max96722-2g.dtb.pre-hs10-20260819`;
- `/lib/modules/6.12.49-lts-next-gdc3c935e12a5/updates/max96724.ko.pre-hs10-final-20260819`;
- `/lib/modules/6.12.49-lts-next-gdc3c935e12a5/updates/max96724.ko.pre-rx4-tx1-20260819`.

This is a cold-boot, repeated-switch, and two-minute preview validation, not a
multi-hour endurance claim.  The prior SOT/FIFO-overflow mechanism was absent
from all final intervals, but a longer product soak remains appropriate.

## Pre-commit repository state

The Linux worktree contained unrelated display and backup files; these were
preserved and excluded.  The final commit scope is limited to the MAX96722/
MAX96717 bindings and drivers, the dedicated board DTS, the board defconfig,
the required i.MX media integration, and this porting log.

## 2026-08-20 shared-driver isolation audit

The MAX96717/MAX96722 workarounds in the common i.MX8 media drivers were
audited to keep OV5640, N4, and other camera paths on their original behavior:

- `imx8-mipi-csi2-sam.c` now enables shared stream reference counting,
  null-safe optional power callbacks, remote active-state frame-interval
  calls, and extra CSIS status registers only when the CSI endpoint's remote
  parent is both `maxim,max96722` and marked with
  `maxim,ali360-yh-profile`;
- the ordinary camera branch retains the original one-callback/one-hardware-
  transition stream behavior and original frame-interval state argument;
- the global `hs_settle_override` module parameter was removed; receiver
  timing remains board-specific through the existing `csis-hs-settle` value;
- the unrelated global RAW10 packed-width change was removed;
- `imx8-media-dev.c` uses the MAX96722 bridge source-pad/VC0 topology and
  one-time nested-notifier link creation only for the same compatible/profile
  pair; all other sensors retain the original endpoint-port pad mapping and
  notifier behavior;
- the camera-unrelated `imx8mp-ldb.c` clone-mask change was removed, leaving
  that common display driver identical to the repository baseline.

Validation completed with:

- `git diff --check`;
- kernel `checkpatch.pl`: zero errors, warnings, or checks for the two changed
  common media drivers;
- successful AArch64 cross-compilation of `imx8-mipi-csi2-sam.o` and
  `imx8-media-dev.o`.

Both the normal and MAX96722 DTB targets currently stop on a pre-existing,
unrelated duplicate `lvds_touch` label in `myd-js8mpq.dts` (lines 878 and
900).  The isolation audit added no new DT property and did not alter that
unrelated display/touchscreen work.

## 2026-08-20 complete build, deployment, and four-camera regression

The repository baseline advanced externally from `5c6f69badd83` to
`8dde7e57a153` while the full build was running.  Image and all modules were
therefore relinked against the final baseline.  The deployed kernel release is
`6.12.49-g8dde7e57a153-dirty`; the MAX96717, MAX96724, and i.MX8 media-device
modules have matching vermagic.

STREAMON testing found one target-version integration defect in the unmodified
NXP ISI caller: `mxc_isi_source_fmt_init()` left the local
`v4l2_subdev_format.stream` field uninitialized.  Legacy OV/N4 subdevices
ignored that field, but the 6.12 routed MAX96722 subdevice rejected the random
stream number before the CSI callback with `-EINVAL`.  The local structure is
now zero-initialized before its fields are assigned.  This removes undefined
stack data without changing any valid sensor format or ordinary-camera branch.

MAX CSI handling is selected by the explicit `maxim,max96717-chain` property
in only `myd-js8mpq-hdmi-max96722-2g.dts`.  Generated-DTB inspection confirms
that the base, OV5640, and OV13855 DTBs do not contain this marker.  The common
media driver still keeps its original stream, power, pad, and notifier paths
for all DTBs without the marker.  `imx8mp-ldb.c` remains identical to the Git
baseline.

Build/static validation:

- full `Image` and `modules` builds completed successfully;
- base, OV5640, OV13855, and MAX96722 DTBs compiled successfully;
- `git diff --check` passed;
- checkpatch on the three changed common i.MX media-driver patches reported
  zero errors, warnings, or checks;
- only the MAX96722 DTB contains `maxim,max96717-chain`.

Deployed on `192.168.40.234`:

- Image SHA-256:
  `d54702a1fd01f168c77d58faf6e900353ff647603e8a7d7998f0369ee161804b`;
- MAX96722 DTB SHA-256:
  `4ff1c79169fc21556d5dc76ee991fd6fb5f3c59f704c552f6332fd1aa95719d1`;
- complete matching module tree:
  `/lib/modules/6.12.49-g8dde7e57a153-dirty`;
- pre-deployment boot backup:
  `/run/media/mmcblk2p1/codex-backup-max96722-20260820-1800`;
- extlinux rollback label: `max96722-rollback`.

Cold-boot and four-camera validation:

- all four GMSL links locked (`configured=0xf`, `locked=0xf`);
- media graph exposed four active 1920x1536@30 MAX96722 routes;
- cameras 0, 1, 2, and 3 each delivered 150 requested 1920x1080 YUYV frames;
- observed rate was 29.99-30.02 fps on every camera;
- each run recorded 151 frame-end and 152 frame-start events;
- ECC, CRC, SOT, FIFO overflow, lost-frame-start, lost-frame-end, and unknown
  error counters were zero on all four runs;
- one 1920x1080 frame from each camera was converted to PNG and visually
  checked: all four show distinct live bench views, with no black frame,
  duplicated channel, red points, horizontal corruption, or green band.

The board was left on camera 0 with CSI debug logging disabled.  This is a
cold-boot and four-channel functional regression, not a multi-hour endurance
test.  These results form the pre-commit acceptance record for the MAX96717/
MAX96722 camera-support change.
