"""Run the dedicated VMM image and require the full guest protocol."""
import subprocess
import sys


def output_text(output):
    return (output or b"").decode("ascii", errors="replace")


def main(iso):
    command = [
        "qemu-system-i386", "-m", "10M", "-cdrom", iso, "-display", "none",
        "-serial", "none", "-monitor", "none", "-no-reboot", "-no-shutdown",
        "-debugcon", "stdio", "-device", "isa-debug-exit,iobase=0xf4,iosize=4",
    ]
    error = None
    try:
        run = subprocess.run(command, capture_output=True, timeout=30, check=False)
        output = output_text(run.stdout)
        if run.returncode != 33:
            error = f"Code QEMU {run.returncode}, attendu 33"
        if run.stderr:
            print(output_text(run.stderr), file=sys.stderr, end="")
    except subprocess.TimeoutExpired as exc:
        output = output_text(exc.stdout)
        error = "QEMU a dépassé le délai de 30 secondes"
    except OSError as exc:
        output = ""
        error = str(exc)

    print(output, end="", flush=True)
    expected = ["VMM BEGIN"]
    for number in range(1, 11):
        expected.extend((f"T{number:02} START", f"T{number:02} PASS"))
    expected.append("VMM END")
    lines = output.splitlines()
    if error is None and lines == expected:
        return 0

    print(error or "Protocole VMM incomplet ou invalide", file=sys.stderr)
    for number in range(1, 11):
        test = f"T{number:02}"
        if f"{test} PASS" in lines:
            status = "PASS"
        elif f"{test} START" in lines or f"{test} FAIL" in lines:
            status = "FAIL"
        else:
            status = "NOT RUN"
        print(f"{test} {status}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
