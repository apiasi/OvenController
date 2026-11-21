"""Compila e executa testes do firmware ARM, sem conectar uma placa.

Dependencias: python -m pip install unicorn pyelftools
Uso: python Tests/run_regression.py [--compiler caminho/arm-none-eabi-gcc.exe]
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                     UC_HOOK_INTR, UC_HOOK_MEM_INVALID)
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_M3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("arm-none-eabi-gcc")
    if not compiler:
        found = list(pathlib.Path("C:/ST").glob(
            "STM32CubeIDE*/STM32CubeIDE/plugins/"
            "com.st.stm32cube.ide.mcu.externaltools.gnu-tools*/tools/bin/arm-none-eabi-gcc.exe"
        ))
        if not found:
            parser.error("Informe --compiler ou adicione arm-none-eabi-gcc ao PATH")
        compiler = str(sorted(found)[-1])
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="oven-regression-") as temporary:
        binary = pathlib.Path(temporary) / "tests.elf"
        command = [compiler, "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft",
                   "-std=gnu11", "-Os", "-Wall", "-Wextra", "-Werror",
                   "-DUSE_HAL_DRIVER", "-DSTM32F103xB", "-ffunction-sections",
                   "-fdata-sections", "-nostdlib", "-Wl,--gc-sections",
                   "-Wl,-e,test_start", "-TSTM32F103C8TX_FLASH.ld"]
        for include in ["Core/Inc", "Drivers/STM32F1xx_HAL_Driver/Inc",
                        "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy",
                        "Drivers/CMSIS/Device/ST/STM32F1xx/Include", "Drivers/CMSIS/Include"]:
            command.append("-I" + include)
        faults = ["NMI_Handler", "HardFault_Handler", "MemManage_Handler",
                  "BusFault_Handler", "UsageFault_Handler", "Error_Handler"]
        command += ["-Wl,--undefined=" + name for name in faults]
        command += ["Tests/oven_regression.c", "-lgcc", "-o", str(binary)]
        subprocess.run(command, cwd=root, check=True)
        machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        machine.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        machine.mem_map(0x08000000, 0x10000)
        machine.mem_map(0x20000000, 0x10000)
        machine.mem_map(0x40000000, 0x30000)
        machine.mem_map(0x42420000, 0x10000)  # Alias bit-band do RCC.
        machine.mem_map(0xE000E000, 0x2000)
        with binary.open("rb") as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
                    machine.mem_write(segment["p_vaddr"], segment.data())
            symbols = {symbol.name: symbol["st_value"]
                       for symbol in elf.get_section_by_name(".symtab").iter_symbols()}
        stopped = []

        def on_interrupt(emulator, number, user_data):
            stopped.append(number)
            emulator.emu_stop()

        machine.hook_add(UC_HOOK_INTR, on_interrupt)
        def on_invalid(emulator, access, address, size, value, user_data):
            print(f"Memoria invalida: endereco={address:#x}, "
                  f"PC={emulator.reg_read(UC_ARM_REG_PC):#x}")
            return False
        machine.hook_add(UC_HOOK_MEM_INVALID, on_invalid)
        machine.reg_write(UC_ARM_REG_SP, 0x20005000)
        machine.emu_start(symbols["test_start"] | 1, 0, timeout=30_000_000,
                          count=10_000_000)
        read = lambda name: int.from_bytes(machine.mem_read(symbols[name], 4), "little")
        if not stopped:
            raise RuntimeError("O teste nao terminou dentro do limite de execucao")
        if read("failedLine"):
            raise AssertionError(f"Falha em Tests/oven_regression.c:{read('failedLine')}")
        if read("testCompleted") != 1:
            raise RuntimeError("A CPU foi interrompida antes de concluir os testes")
        count = read("testCount")
        for name in faults:
            # Emula a entrada direta do handler e verifica o corte por registrador.
            machine.mem_write(0x40010C14, bytes(4))  # GPIOB BRR
            machine.mem_write(0x40010C00, bytes(4))  # GPIOB CRL
            machine.reg_write(UC_ARM_REG_SP, 0x20005000)
            machine.emu_start(symbols[name] | 1, 0, count=100)
            brr = int.from_bytes(machine.mem_read(0x40010C14, 4), "little")
            crl = int.from_bytes(machine.mem_read(0x40010C00, 4), "little")
            if brr != 1 or (crl & 0xF) != 2:
                raise AssertionError(f"SSR nao desligado por {name}")
        print(f"OK: {count} verificacoes do codigo ARM e {len(faults)} handlers passaram.")


if __name__ == "__main__":
    main()
