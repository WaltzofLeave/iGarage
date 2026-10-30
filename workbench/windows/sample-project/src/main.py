"""A little focus. A lot of possibilities."""

from dataclasses import dataclass
from utils import greeting, progress_bar


@dataclass
class Task:
    name: str
    completed: bool = False


class Workspace:
    """A small space for your next big idea."""

    def __init__(self, name: str):
        self.name = name
        self.tasks: list[Task] = []

    def add_task(self, name: str) -> None:
        self.tasks.append(Task(name))

    def summary(self) -> None:
        print(greeting(self.name))
        for task in self.tasks:
            status = "✓" if task.completed else "○"
            print(f"  {status}  {task.name}")
        done = sum(t.completed for t in self.tasks)
        print(f"\n  {progress_bar(done, len(self.tasks))}")


if __name__ == "__main__":
    workspace = Workspace("Orbit")
    workspace.add_task("Build something wonderful")
    workspace.add_task("Make every line count")
    workspace.add_task("Stay curious, keep creating")
    workspace.tasks[0].completed = True
    workspace.summary()
