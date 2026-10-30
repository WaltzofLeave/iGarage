"""The little things that make a difference."""


def greeting(name: str) -> str:
    return f"\n  Welcome to {name}. Let's make something great.\n"


def progress_bar(completed: int, total: int, width: int = 20) -> str:
    if total <= 0:
        return "No tasks yet. Your canvas is empty."
    ratio = min(max(completed / total, 0), 1)
    filled = int(width * ratio)
    bar = "━" * filled + "─" * (width - filled)
    return f"{bar}  {completed}/{total} completed ({ratio:.0%})"
