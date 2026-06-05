param(
    [string]$ToolchainBin = "C:\ST\STM32CubeIDE_1.8.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Build = Join-Path $Root "Bootloader\Build"
New-Item -ItemType Directory -Force -Path $Build | Out-Null

$Gcc = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
$Size = Join-Path $ToolchainBin "arm-none-eabi-size.exe"
$Objcopy = Join-Path $ToolchainBin "arm-none-eabi-objcopy.exe"

$CommonFlags = @(
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-mfloat-abi=soft",
    "-DUSE_HAL_DRIVER",
    "-DSTM32U031xx",
    "-DNDEBUG",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Wextra",
    "-Os",
    "-g0",
    "-IBootloader/Inc",
    "-ICustom_lib/inc",
    "-ICore/Inc",
    "-INFC7",
    "-INFC7/Target",
    "-IDrivers/STM32U0xx_HAL_Driver/Inc",
    "-IDrivers/STM32U0xx_HAL_Driver/Inc/Legacy",
    "-IDrivers/CMSIS/Device/ST/STM32U0xx/Include",
    "-IDrivers/CMSIS/Include",
    "-IDrivers/BSP/Components/st25dvxxkc"
)

$Sources = @(
    "Bootloader/Src/ota_bootloader_main.c",
    "Bootloader/Src/ota_bootloader_it.c",
    "Core/Src/custom_bus.c",
    "Core/Src/system_stm32u0xx.c",
    "Core/Startup/startup_stm32u031k8ux.s",
    "Custom_lib/src/ota_shared.c",
    "NFC7/custom_nfc07a1.c",
    "NFC7/custom_nfc07a1_nfctag.c",
    "Drivers/BSP/Components/st25dvxxkc/st25dvxxkc.c",
    "Drivers/BSP/Components/st25dvxxkc/st25dvxxkc_reg.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_cortex.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_exti.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_flash.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_flash_ex.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_gpio.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_i2c.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_i2c_ex.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_pwr.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_pwr_ex.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rcc.c",
    "Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rcc_ex.c"
)

$Objects = @()
foreach ($Source in $Sources) {
    $ObjName = ($Source -replace '[:/\\]', '_') + ".o"
    $Obj = Join-Path $Build $ObjName
    & $Gcc @CommonFlags -c (Join-Path $Root $Source) -o $Obj
    $Objects += $Obj
}

$Elf = Join-Path $Build "ota_bootloader.elf"
$Map = Join-Path $Build "ota_bootloader.map"
$Bin = Join-Path $Build "ota_bootloader.bin"
$Linker = Join-Path $Root "Bootloader\STM32U031K8UX_BOOTLOADER.ld"

$LinkFlags = @(
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-mfloat-abi=soft",
    "-T",
    $Linker,
    "-Wl,-Map=$Map",
    "-Wl,--gc-sections",
    "-static",
    "--specs=nosys.specs",
    "--specs=nano.specs"
)

& $Gcc @LinkFlags @Objects "-Wl,--start-group" "-lc" "-lm" "-Wl,--end-group" "-o" $Elf
& $Objcopy -O binary $Elf $Bin
& $Size $Elf
