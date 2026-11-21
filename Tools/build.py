"""Compila o firmware a partir de um clone, sem makefiles gerados pelo CubeIDE.

Uso: python Tools/build.py --configuration all
Requer GNU Arm Embedded com bibliotecas newlib. O compilador pode estar no PATH,
na instalacao do CubeIDE em C:/ST ou ser informado por --compiler.
"""
import argparse
import json
import pathlib
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release", "all"], default="Release")
    parser.add_argument("--compiler", help="Caminho para arm-none-eabi-gcc")
    parser.add_argument("--configure-only", action="store_true",
                        help="Gera a base de compilacao do editor sem compilar o firmware")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("arm-none-eabi-gcc")
    if not compiler:
        candidates = sorted(pathlib.Path("C:/ST").glob(
            "STM32CubeIDE*/STM32CubeIDE/plugins/"
            "com.st.stm32cube.ide.mcu.externaltools.gnu-tools*/tools/bin/arm-none-eabi-gcc.exe"
        ))
        if not candidates:
            parser.error("Informe --compiler ou adicione arm-none-eabi-gcc ao PATH")
        compiler = str(candidates[-1])
    compiler = pathlib.Path(shutil.which(compiler) or compiler).resolve()
    root = pathlib.Path(__file__).resolve().parent.parent
    sources = sorted((root / "Core/Src").glob("*.c"))
    sources += sorted((root / "Drivers/STM32F1xx_HAL_Driver/Src").glob("*.c"))
    sources.append(root / "Core/Startup/startup_stm32f103c8tx.s")
    includes = ["Core/Inc", "Drivers/STM32F1xx_HAL_Driver/Inc",
                "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy",
                "Drivers/CMSIS/Device/ST/STM32F1xx/Include", "Drivers/CMSIS/Include"]
    configurations = ["Debug", "Release"] if args.configuration == "all" else [args.configuration]
    for configuration in configurations:
        output = root / "Build" / configuration
        output.mkdir(parents=True, exist_ok=True)
        binary = output / "OvenController.elf"
        flags = ["-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft",
                   "-std=gnu11", "-DUSE_HAL_DRIVER", "-DSTM32F103xB", "-Wall", "-Werror",
                   "-ffunction-sections", "-fdata-sections"]
        flags += ["-O0", "-g3", "-DDEBUG"] if configuration == "Debug" else ["-Os"]
        flags += ["-I" + str(root / include) for include in includes]
        # O editor recebe os mesmos parametros de arquitetura, macros e includes.
        # Parametros exclusivos da linkagem nao pertencem aos comandos de analise.
        database = [{"directory": str(root), "file": str(source),
                     "arguments": [str(compiler), *flags, "-c", str(source),
                                   "-o", str(output / (source.stem + ".o"))]}
                    for source in sources if source.suffix == ".c"]
        test_source = root / "Tests/oven_regression.c"
        database.append({"directory": str(root), "file": str(test_source),
                         "arguments": [str(compiler), *flags, "-DOVEN_TEST_REAL_UI",
                                       "-c", str(test_source)]})
        (output / "compile_commands.json").write_text(
            json.dumps(database, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Base do editor gerada em {output / 'compile_commands.json'}", flush=True)
        if args.configure_only:
            continue
        command = [str(compiler), *flags, "--specs=nosys.specs",
                   "--specs=nano.specs", "-static", "-Wl,--gc-sections",
                   "-T" + str(root / "STM32F103C8TX_FLASH.ld"),
                   "-Wl,-Map=" + str(output / "OvenController.map")]
        command += [str(source) for source in sources]
        command += ["-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group", "-o", str(binary)]
        print(f"Compilando {configuration} ({len(sources)} fontes)...", flush=True)
        subprocess.run(command, cwd=root, check=True)
        tool = lambda name: compiler.with_name(compiler.stem.replace("gcc", name) + compiler.suffix)
        subprocess.run([str(tool("size")), str(binary)], check=True)
        for format_name, suffix in [("ihex", ".hex"), ("binary", ".bin")]:
            subprocess.run([str(tool("objcopy")), "-O", format_name, str(binary),
                            str(binary.with_suffix(suffix))], check=True)
        print(f"Arquivos gerados em {output}", flush=True)


if __name__ == "__main__":
    main()
