# Phase 16 Step 2: ARX3A0 카메라 진단 결과 (2026-05-27)

## 세션 정보
- **날짜**: 2026-05-27 (화)
- **시간**: 09:50 ~ 15:00 (점심 포함)
- **작업자**: Jake (jake@flickdone.com)
- **프로젝트**: Em-boxer T1 (orinu-ai)
- **보드**: AE822FA0E5597LS0 DevKit-e8 Rev B Eagle
- **Branch**: `phase15b/emboxer-t1`

## 목표
ARX3A0 카메라 무응답 (Soft Reset I2C fail, CHIP_ID = 0) 진단 + 해결

---

## 🎯 환경 발견 (★ Critical)

### 정확한 cmake 명령
```bash
cmake -B build_obj_det_hp_lite -S . \
  -DTARGET_PLATFORM:STRING=alif \
  -DTARGET_BOARD:STRING=DevKit-e8 \
  -DTARGET_SUBSYSTEM:STRING=RTSS-HP \
  -DUSE_CASE_BUILD:STRING=alif_object_detection \
  -DUSE_FAKE_CAMERA:BOOL=OFF \
  -DALIF_CAMERA_MODULE:STRING=ARX3A0 \
  -DMLEK_LOG_ENABLE:BOOL=OFF \
  -Dalif_object_detection_MODEL_IN_EXT_FLASH:BOOL=ON
```

### 빌드 옵션 (★ 모두 필수)
- `MLEK_LOG_ENABLE=OFF` — 필수 (ON 시 ~3KB overflow)
- `MODEL_IN_EXT_FLASH=ON` — ★ 필수 (~433KB 절약, OFF 시 link 실패)
- `SE_SERVICES_SUPPORT=ON` — alif platform default
- `TARGET_PLATFORM=alif` — 명시 필요 (없으면 mps3 default)

---

## ✅ 수행한 SW 작업

### 1. `board_clocks_config(HFOSC | CLK_100M)` 호출 추가
- 위치: `hal_camera_alif.c::hal_camera_init()`
- DevKit-e8 main.c template과 동일
- **결과**: return 0 (SUCCESS)

### 2. `vbat_init` 코드 추가 (MIPI DPHY power + isolation)
- DevKit-e8 main.c template 본문 그대로
- **결과**: VBAT->PWR_CTRL: `0x20003000 → 0x00052000`
- **발견**: ISO bits 이미 0였음 (SE/Boot가 이미 해제)

### 3. Camera pinmux 3개 추가 (`hal_camera_init`)
- P0_3 alt 6 (CAM_XVCLK_A)
- P7_2 alt 5 (I2C1_SDA_C)
- P7_3 alt 5 (I2C1_SCL_C)

### 4. `board_pins_config` 호출 ★ 복원
- 위치: `platform_drivers.c`
- Phase 15a A4-v4e에서 ETH 보호 위해 제거됨
- Phase 16에서 다시 활성화 (`#if 1`)
- pins.h 전체 array 적용 (P0_3 pad_control 4mA drive 포함)

### 5. ARX3A0 driver markers 추가
- Submodule: `cmsis-alif/components/Source/arx3A0_camera_sensor.c`
- `ARX3A0_Camera_Hard_Reseten` markers (0xE2xxxxxx) — SUCCESS
- `ARX3A0_Sensor_Enable_Clk_Src` markers (0xE3xxxxxx) — SUCCESS

---

## 📊 진단 결과

### ✅ 검증된 사항
| Item | Marker | Status |
|------|--------|--------|
| board_clocks_config(HFOSC+CLK_100M) | 0x027DC0F8 = 0 | SUCCESS |
| VBAT->PWR_CTRL 변경 | 0x20003000→0x00052000 | OK |
| ARX3A0 Hard_Reset | 0x027DC110 = 0xE20000FF | SUCCESS |
| set_cpi_pixel_clk 호출 | 0x027DC120 = 0xE30000FF | SUCCESS |
| hp_phase15b_step1_init 완료 | 0x027DC400 = 0xCAFE000B | OK |
| board_pins_config 호출 | (복원됨) | OK |
| HE NPU KWS 작동 | inf_count 증가 (0xBB3DEAF6) | OK |

### ✗ 실패 사항
| Item | Status |
|------|--------|
| P0_3 (CAM_XVCLK_A) 측정 | **0V** (clock 안 나옴) |
| ARX3A0 Soft_Reset I2C | timeout (0xE1000005) |
| CHIP_ID readback | 0x00000000 (expected 0x0353) |

---

## ★★★ 결론: Hardware Issue 확정
---

## ☕ 다음 세션 (Phase 16 Step 3) 시작점

### Action Items
1. ☐ Alif Support 답변 확인 (메일 보냄: 2026-05-27)
2. ☐ Schematic 220-00319-B J16 connector pinout 검토
3. ☐ P0_3 (R11 pin) oscilloscope 측정 (clock signal 진짜 X?)
4. ☐ 가능하면 다른 DevKit-e8 보드 시도
5. ☐ MIPI CSI2 lanes 추가 pinmux 검토 (만약 missing)
6. ☐ ETH 영향 검증 (board_pins_config 복원 후)

### 다음 세션 즉시 명령
```bash
cd ~/alif_e8_build/alif_ml-embedded-evaluation-kit

# 1. ETH 영향 확인
ip addr show eth0
ping -c 1 8.8.8.8

# 2. 진단 결과 dump
ssh root@<emboxer-ip>
devmem 0x027DC0E0    # Camera HAL state
devmem 0x027DC0F8    # board_clocks_config result
devmem 0x027DC0FC    # VBAT before
devmem 0x027DC0FE    # VBAT after
devmem 0x027DC100    # ARX3A0_Init step
devmem 0x027DC108    # CHIP_ID
devmem 0x027DC110    # Hard_Reset
devmem 0x027DC120    # Sensor_Enable_Clk_Src
devmem 0x027DC400    # HP marker
devmem 0x027DC550    # inf_count

# P0_3 측정 — 멀티미터 (★ ~0.9V 기대)
```

### 환경 확인
```bash
# Build 정확한 명령
cmake -B build_obj_det_hp_lite -S . \
  -DTARGET_PLATFORM:STRING=alif \
  -DTARGET_BOARD:STRING=DevKit-e8 \
  -DTARGET_SUBSYSTEM:STRING=RTSS-HP \
  -DUSE_CASE_BUILD:STRING=alif_object_detection \
  -DUSE_FAKE_CAMERA:BOOL=OFF \
  -DALIF_CAMERA_MODULE:STRING=ARX3A0 \
  -DMLEK_LOG_ENABLE:BOOL=OFF \
  -Dalif_object_detection_MODEL_IN_EXT_FLASH:BOOL=ON
```

---

## 📦 변경 파일 요약

### 메인 repo (`alif_ml-embedded-evaluation-kit`)
- `source/hal/source/components/camera/source/alif/source/hal_camera_alif.c`
  - `#include "board_config.h"`, `#include "soc.h"`, `#include "pinconf.h"`
  - `board_clocks_config()` 호출
  - `vbat_init` 코드 인라인
  - Camera pinmux 3개 (XVCLK + I2C SDA/SCL)
  - 진단 markers (0x027DC0E0 ~ 0x027DC0FE)
  
- `source/hal/source/platform/alif/source/platform_drivers.c`
  - `board_pins_config()` 호출 복원 (HP에서)
  - `board_gpios_config()` 호출 복원

### Submodule `cmsis-alif`
- `components/Source/arx3A0_camera_sensor.c` (+84 lines markers)
  - `ARX3A0_Camera_Hard_Reseten` markers
  - `ARX3A0_Sensor_Enable_Clk_Src` markers

### Backup files (참고용, 작동 baseline)
- `platform_drivers.c.before_A4_fix` (Phase 15a A4-v4 fix 이전)
- `platform_drivers.c.before_step1_v2` (Phase 15b Step 1 이전)

---

## 📧 Alif Support 메일 (2026-05-27 발송)

**Subject**: ARX3A0 camera unresponsive on DevKit-e8 (working on AppKit-e7) - Phase 16 diagnostic

**핵심**:
- 같은 ARX3A0 module E7에서 작동, E8 무응답
- 모든 SW setup OK, P0_3 = 0V
- ARX3A0_Soft_Reset I2C write fail
- Sensor power 정상 (TP30=1.21V, TP31=2.8V)

---

## 🎯 누적 진척도
**Last updated**: 2026-05-27 15:00
