from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
README_BANNER = """# embedded-llama overlay

This fork keeps upstream `llama.cpp` intact and adds an embedded, no-HTTP CLI (`llama-embedded-cli`) so chat, completion, embeddings, rerank, tokenize, etc. can run in-process without starting `llama-server`. The original upstream README begins below for reference.

---

"""

CMAKE_NEEDLE = "add_subdirectory(embedded-cli)"
CMAKE_SERVER_LINE = "add_subdirectory(server)"

RELEASE_WORKFLOW = REPO / ".github" / "workflows" / "release.yml"
# Jobs we keep disabled but leave in the file for future use
DISABLED_JOBS = ["windows-sycl:", "windows-hip:", "macOS-x64:", "ios-xcode-build:"]
# Jobs that must finish before publishing a release
REQUIRED_NEEDS = [
    "ubuntu-22-cuda",
    "ubuntu-22-cpu",
    "ubuntu-22-vulkan",
    "windows",
    "windows-cpu",
    "windows-cuda",
    "macOS-arm64",
]


def ensure_readme_banner() -> bool:
    readme = REPO / "README.md"
    data = readme.read_text(encoding="utf-8")
    if data.startswith(README_BANNER):
        return False
    if data.startswith("# llama.cpp"):
        readme.write_text(README_BANNER + data, encoding="utf-8")
        return True
    raise SystemExit(f"README.md header not recognized; manual merge needed (found: {data[:40]!r})")


def ensure_cmake_hook() -> bool:
    path = REPO / "tools" / "CMakeLists.txt"
    data = path.read_text(encoding="utf-8")
    if CMAKE_NEEDLE in data:
        return False

    if CMAKE_SERVER_LINE not in data:
        raise SystemExit("tools/CMakeLists.txt does not contain add_subdirectory(server); manual merge needed")

    new_data = data.replace(
        CMAKE_SERVER_LINE,
        f"{CMAKE_SERVER_LINE}\n        {CMAKE_NEEDLE}",
        1,
    )
    if new_data == data:
        raise SystemExit("Failed to inject embedded-cli hook; manual merge needed")

    path.write_text(new_data, encoding="utf-8")
    return True


def ensure_release_workflow() -> bool:
    if not RELEASE_WORKFLOW.exists():
        return False

    data = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    changed = False

    # Ensure CUDA install includes compiler and nvcc env is set
    if "cuda-compiler-12-4" not in data:
        data = data.replace(
            "sudo apt-get install -y cuda-toolkit-12-4",
            "sudo apt-get install -y cuda-toolkit-12-4 cuda-compiler-12-4",
            1,
        )
        changed = True

    if "CUDACXX: /usr/local/cuda/bin/nvcc" not in data:
        needle = (
            "      - name: Build\n"
            "        id: cmake_build\n"
            "        run: |\n"
            "          cmake -B build \\\n"
        )
        if needle not in data:
            raise SystemExit("release.yml format changed; cannot insert CUDACXX env")
        env_block = (
            "      - name: Build\n"
            "        id: cmake_build\n"
            "        env:\n"
            "          CUDACXX: /usr/local/cuda/bin/nvcc\n"
            "          CUDA_HOME: /usr/local/cuda\n"
            "          PATH: /usr/local/cuda/bin:${PATH}\n"
            "        run: |\n"
            "          cmake -B build \\\n"
        )
        data = data.replace(needle, env_block, 1)
        changed = True

    # Remove s390x matrix entry and opencl-adreno backend if present
    data_old = data
    data = data.replace("          - build: 's390x'\n            os: ubuntu-24.04-s390x\n", "")
    data = data.replace(
        "          - backend: 'opencl-adreno'\n            arch: 'arm64'\n            defines: '-G \"Ninja Multi-Config\" -D CMAKE_TOOLCHAIN_FILE=cmake/arm64-windows-llvm.cmake -DCMAKE_PREFIX_PATH=\"$env:RUNNER_TEMP/opencl-arm64-release\" -DGGML_OPENCL=ON -DGGML_OPENCL_USE_ADRENO_KERNELS=ON'\n            target: 'ggml-opencl'\n",
        "",
    )
    if data != data_old:
        changed = True

    # Ensure needs list matches REQUIRED_NEEDS
    needs_prefix = "    needs:\n"
    if needs_prefix not in data:
        raise SystemExit("release.yml missing needs block for release job")
    start = data.index(needs_prefix) + len(needs_prefix)
    remainder = data[start:]
    end_rel = remainder.index("\n\n")
    current_block = remainder[:end_rel]
    needs_lines = [line.strip() for line in current_block.splitlines() if line.strip().startswith("- ")]
    current_needs = [line[2:] for line in needs_lines]
    if current_needs != REQUIRED_NEEDS:
        new_block = "".join(f"      - {n}\n" for n in REQUIRED_NEEDS)
        data = data[:start] + new_block + remainder[end_rel:]
        changed = True

    # Disable optional jobs
    for job in DISABLED_JOBS:
        marker = f"{job}\n    if: ${{ false }}"
        if marker not in data and job in data:
            data = data.replace(job, marker, 1)
            changed = True

    if changed:
        RELEASE_WORKFLOW.write_text(data, encoding="utf-8")
    return changed


def main() -> None:
    changed = []
    if ensure_readme_banner():
        changed.append("README.md (banner)")
    if ensure_cmake_hook():
        changed.append("tools/CMakeLists.txt (embedded-cli hook)")
    if ensure_release_workflow():
        changed.append(".github/workflows/release.yml (release defaults)")

    if not changed:
        print("Nothing to reapply; overlays already present.")
    else:
        print("Reapplied overlays: " + ", ".join(changed))


if __name__ == "__main__":
    main()
