"""Wrapper for compiled binaries."""
import pathlib
import shutil
import subprocess
import sys
import typing

if typing.TYPE_CHECKING:
    from os import PathLike
    from pathlib import Path
    from typing import Optional, Protocol, Sequence, Union

    _CMD = Optional[Sequence[Union[str, bytes, Path, PathLike]]]

    class _BinaryWrapper(Protocol):
        def __call__(self, args: _CMD = None, **kwargs) -> subprocess.CompletedProcess:
            ...


_binaries = ["arg_likelihood", "arg_sample", "arg_summarize"]
__all__ = _binaries + ["require_executable"]

arg_likelihood: "_BinaryWrapper"
"Sampler for large ancestral recombination graphs"
arg_sample: "_BinaryWrapper"
"Sampler for large ancestral recombination graphs"
arg_summarize: "_BinaryWrapper"
"Summarize the output of ARGweaver."

_bin_dir = pathlib.Path(__file__).parent.parent / "bin"


def _bin_wrapper(path: "Union[Path, str]", **kwargs) -> "_BinaryWrapper":
    def f(args=None, **kw):
        new_kw = kwargs.copy()
        new_kw.update(kw)
        return_process = new_kw.pop("return_process", True)
        p = subprocess.run(
            [path, *(args or sys.argv[1:])],
            **new_kw,
        )
        if return_process:
            return p

    return f


for _ in _binaries:
    _bin = _bin_dir / _.replace("_", "-")
    if not _bin.exists():
        raise RuntimeError(f"Missing binary: {_bin}")
    globals()[_] = _bin_wrapper(_bin, capture_output=False, return_process=False)


def require_executable(executable: str, additional_message: str = "", **kwargs):
    """Require an executable to be available and return a wrapper for it.

    Parameters
    ----------
    executable : {py:obj}`str`
        Name of the executable.

    additional_message : {py:obj}`str`, optional
        Additional message to include in the exception.

    return_process : {py:obj}`bool`, optional
        Whether to return the process. Default is {py:obj}`True`.

    **kwargs
        Additional keyword arguments to pass to the {py:func}`subprocess.run`."""
    exe = shutil.which(executable)
    if exe is None:
        raise FileNotFoundError(
            " ".join([f"`{executable}` is required but not found.", additional_message])
        )
    return _bin_wrapper(exe, **kwargs)