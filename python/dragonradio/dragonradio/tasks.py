# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Utilities for managing asyncio tasks"""
import asyncio
from asyncio import AbstractEventLoop, Task
import io
import logging
from logging import Logger
from typing import List, Optional

class TaskManager:
    """Manage asyncio tasks"""
    loop: Optional[AbstractEventLoop]=None
    """asyncio event loop"""

    tasks: List[Task]
    """A list of asyncio tasks"""

    def __init__(self, loop: Optional[AbstractEventLoop]=None):
        self.loop = loop
        self.tasks = []

    def createTask(self, task: Task, name: Optional[str]=None):
        """Create and add a task to event loop"""
        async def task_wrapper(task: Task) -> Task:
            try:
                return await task
            except:
                logging.exception("Exception in task")
                raise

        def f():
            assert self.loop is not None
            self.addTask(self.loop.create_task(task_wrapper(task), name=name))

        assert self.loop is not None
        self.loop.call_soon_threadsafe(f)

    def addTask(self, task: Task):
        """Add a task to event loop"""
        # We call ensure_future to catch any illegal "tasks" earlier
        self.tasks.append(asyncio.ensure_future(task))

    async def stopTasks(self, logger: Optional[Logger]=None):
        """Cancel all tasks and wait for them to finish"""
        if logger is None:
            logger = logging.getLogger()

        for task in self.tasks:
            task.cancel()

        results = await asyncio.gather(*self.tasks, return_exceptions=True)

        for task in self.tasks:
            # XXX A task may not yet be done despite the fact that we awaited on
            # gather!
            if not task.cancelled() and (task.done() and task.exception() is not None):
                f = io.StringIO()
                task.print_stack(file=f)
                logger.error(f.getvalue())

        return results

async def stopEventLoop(loop: AbstractEventLoop, logger: Optional[Logger]=None):
    """Wait for tasks to finish and then stop the event loop"""
    if logger is None:
        logger = logging.getLogger()

    # Get all running tasks
    tasks = list(asyncio.all_tasks(loop))

    # Remove this task from the lists of tasks
    this_task = asyncio.current_task(loop)
    assert this_task is not None
    tasks.remove(this_task)

    # Cancel unfinished tasks
    unfinished_tasks = [t for t in tasks if not t.done()]

    if len(unfinished_tasks) != 0:
        logger.debug('Unfinished tasks: %s', unfinished_tasks)

        # Cancel unfinished tasks
        logger.info('Cancelling unfinished tasks')

        for _task in unfinished_tasks:
            pass #task.cancel()

    # Wait for unfinished tasks
    await asyncio.gather(*tasks, return_exceptions=True)

    # Stop event loop
    logger.info('Stopping event loop')
    loop.stop()
