"""Invalidate environment CMake metadata after manifest/resource input changes."""
import hashlib
import importlib.util
from pathlib import Path

MARKER = ".racool-manifest-sha256"


def manifest_digest(project):
    project = Path(project)
    digest = hashlib.sha256()
    inputs = ["idf_component.yml", "src/idf_component.yml", "package-lock.json",
              "tools/generate_translations.py", "tools/generate_oled_fonts.mjs",
              "tools/product_resources.cmake", "resources/fonts/NotoSans-SemiBold.ttf",
              "tools/product_version.py", "resources/product-version.json"]
    inputs += [path.relative_to(project).as_posix()
               for path in sorted((project / "resources/translations").glob("*.json"))]
    # Existing C consumers can change a literal format contract without changing
    # catalog JSON. Unrelated source edits do not invalidate resource metadata.
    inputs += [path.relative_to(project).as_posix()
               for path in sorted((project / "src").rglob("*.c")) if b"LOC_PRINTF" in path.read_bytes()]
    for relative in inputs:
        path = project / relative
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(path.read_bytes() if path.is_file() else b"<absent>")
        digest.update(b"\0")
    spec = importlib.util.spec_from_file_location("racool_product_version", project / "tools/product_version.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    digest.update(repr(module.git_identity(project)).encode("utf-8"))
    return digest.hexdigest()


def prepare_cache(project, output):
    output = Path(output)
    current = manifest_digest(project)
    marker = output / MARKER
    previous = marker.read_text(encoding="ascii").strip() if marker.is_file() else None
    cache = output / "CMakeCache.txt"
    if current != previous and cache.is_file():
        cache.unlink()
    return current


def record_cache(output, digest):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / (MARKER + ".tmp")
    temporary.write_text(digest + "\n", encoding="ascii")
    temporary.replace(output / MARKER)


def configure(env):
    project = Path(env.subst("$PROJECT_DIR"))
    output = Path(env.subst("$BUILD_DIR"))
    digest = prepare_cache(project, output)

    def accept_configuration(target, source, env):
        # checkprogsize is an AlwaysBuild alias, including unchanged programs.
        # Failure before this point leaves the old marker for a safe retry.
        record_cache(output, digest)

    env.AddPostAction("checkprogsize", accept_configuration)


try:
    Import("env")
except NameError:
    pass
else:
    configure(env)
