"""Compila e executa testes do firmware ARM, sem conectar uma placa.

Dependencias: python -m pip install unicorn pyelftools
Uso: python Tests/run_regression.py [--compiler caminho/arm-none-eabi-gcc.exe]
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import struct
import zlib

from elftools.elf.elffile import ELFFile
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                     UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_HOOK_CODE)
from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_M3,
                               UC_ARM_REG_R1, UC_ARM_REG_R2)


def save_png(path, pixels):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + pixels[y * 960:(y + 1) * 960] for y in range(240))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler")
    parser.add_argument("--scenario", choices=["regression", "inputs", "profiles", "preview"], default="regression")
    parser.add_argument("--output", default="Tests/previews")
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
        entry = {"inputs": "input_test_start", "profiles": "profile_test_start",
                 "preview": "preview_start"}.get(args.scenario, "test_start")
        command = [compiler, "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft",
                   "-std=gnu11", "-Os", "-Wall", "-Wextra", "-Werror",
                   "-DUSE_HAL_DRIVER", "-DSTM32F103xB", "-ffunction-sections",
                   "-fdata-sections", "-nostdlib", "-Wl,--gc-sections",
                   "-Wl,-e," + entry, "-TSTM32F103C8TX_FLASH.ld"]
        if args.scenario in ["inputs", "preview"]:
            command += ["-DOVEN_TEST_REAL_UI", "--specs=nano.specs"]
        for include in ["Core/Inc", "Drivers/STM32F1xx_HAL_Driver/Inc",
                        "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy",
                        "Drivers/CMSIS/Device/ST/STM32F1xx/Include", "Drivers/CMSIS/Include"]:
            command.append("-I" + include)
        faults = ["NMI_Handler", "HardFault_Handler", "MemManage_Handler",
                  "BusFault_Handler", "UsageFault_Handler", "Error_Handler"]
        command += ["-Wl,--undefined=" + name for name in faults]
        command += ["Tests/oven_regression.c", "-Wl,--start-group", "-lc", "-lgcc",
                    "-lnosys", "-Wl,--end-group", "-o", str(binary)]
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
        read = lambda name: int.from_bytes(machine.mem_read(symbols[name], 4), "little")
        if args.scenario == "preview":
            output = pathlib.Path(args.output).resolve()
            output.mkdir(parents=True, exist_ok=True)
            pixels = bytearray(320 * 240 * 3)
            captured = [0]
            window = {"x0": 0, "x1": 319, "y0": 0, "y1": 239, "x": 0, "y": 0, "cmd": 0}

            def capture_spi(emulator, address, size, user_data):
                captured[0] += 1
                length = emulator.reg_read(UC_ARM_REG_R2)
                data = bytes(emulator.mem_read(emulator.reg_read(UC_ARM_REG_R1), length))
                odr = int.from_bytes(emulator.mem_read(0x4001080C, 4), "little")
                if not (odr & 8):
                    window["cmd"] = data[0]
                    if data[0] == 0x2C:
                        window["x"], window["y"] = window["x0"], window["y0"]
                elif window["cmd"] in [0x2A, 0x2B] and len(data) == 4:
                    axis = "x" if window["cmd"] == 0x2A else "y"
                    window[axis + "0"], window[axis + "1"] = struct.unpack(">HH", data)
                elif window["cmd"] == 0x2C:
                    for offset in range(0, len(data) - 1, 2):
                        value = (data[offset] << 8) | data[offset + 1]
                        x, y = window["x"], window["y"]
                        if 0 <= x < 320 and 0 <= y < 240:
                            index = (y * 320 + x) * 3
                            pixels[index:index + 3] = bytes([
                                ((value >> 11) & 31) * 255 // 31,
                                ((value >> 5) & 63) * 255 // 63,
                                (value & 31) * 255 // 31])
                        window["x"] += 1
                        if window["x"] > window["x1"]:
                            window["x"], window["y"] = window["x0"], y + 1

            address = symbols["HAL_SPI_Transmit"] & ~1
            machine.hook_add(UC_HOOK_CODE, capture_spi, begin=address, end=address)

        def on_interrupt(emulator, number, user_data):
            if args.scenario == "preview":
                labels = {1: "perfil", 2: "editor", 3: "execucao", 4: "resfriamento",
                          5: "secagem", 6: "falha", 7: "secagem_aquecendo", 8: "secagem_executando"}
                label = labels[read("previewId")]
                save_png(output / (label + ".png"), pixels)
                if not read("testCompleted"):
                    pc = emulator.reg_read(UC_ARM_REG_PC)
                    if bytes(emulator.mem_read(pc, 2)) == b"\x00\xbe":
                        emulator.reg_write(UC_ARM_REG_PC, pc + 2 | 1)
                    return
            stopped.append(number)
            emulator.emu_stop()

        machine.hook_add(UC_HOOK_INTR, on_interrupt)
        def on_invalid(emulator, access, address, size, value, user_data):
            print(f"Memoria invalida: endereco={address:#x}, "
                  f"PC={emulator.reg_read(UC_ARM_REG_PC):#x}")
            return False
        machine.hook_add(UC_HOOK_MEM_INVALID, on_invalid)
        machine.reg_write(UC_ARM_REG_SP, 0x20005000)
        machine.emu_start(symbols[entry] | 1, 0, timeout=30_000_000,
                          count=50_000_000)
        if not stopped:
            raise RuntimeError("O teste nao terminou dentro do limite de execucao")
        if "failedLine" in symbols and read("failedLine"):
            raise AssertionError(f"Falha em Tests/oven_regression.c:{read('failedLine')}")
        if read("testCompleted") != 1:
            raise RuntimeError("A CPU foi interrompida antes de concluir os testes")
        count = read("testCount") if "testCount" in symbols else 0
        if args.scenario == "preview":
            print(f"Oito telas renderizadas em {output}; transferencias capturadas: {captured[0]}")
            return
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
        if args.scenario == "inputs":
            print(f"SPI simulado a 9 Mbit/s: START={read('measuredStartLatency')} ms, "
                  f"STOP={read('measuredStopLatency')} ms apos inicio da pressao.")
            print(f"Atualizacao de um valor do encoder: {read('measuredCardBytes')} bytes SPI.")
            print("Tempos incluem debounce e transmissao simulada; ciclos da CPU nao modelados.")


if __name__ == "__main__":
    main()
