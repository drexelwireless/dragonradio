# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Utilities for managing asyncio tasks"""
import asyncio
import io
import logging

class TaskManager:
    """Manage a list of asyncio tasks"""
    def __init__(self, loop=None):
        self.loop = loop
        """asyncio event loop"""

        self.tasks = []
        """A list of asyncio tasks"""

    def createTask(self, task, name=None):
        """Create and add a task to event loop"""
        def f():
            self.addTask(self.loop.create_task(task, name=name))

        self.loop.call_soon_threadsafe(f)

    def addTask(self, task):
        """Add a task to event loop"""
        # We call ensure_future to catch any illegal "tasks" earlier
        self.tasks.append(asyncio.ensure_future(task))

    async def stopTasks(self, logger=logging.getLogger()):
        """Cancel all tasks and wait for them to finish"""
        for task in self.tasks:
            task.cancel()

        results = await asyncio.gather(*self.tasks, return_exceptions=True)

        for task in self.tasks:
            if not task.cancelled and task.exception() is not None:
                f = io.StringIO()
                task.print_stack(file=f)
                logger.error(f.getvalue())

        return results

async def stopEventLoop(loop, logger=logging.getLogger()):
    """Wait for tasks to finish and then stop the event loop"""
    tasks = list(asyncio.all_tasks(loop))
    tasks.remove(asyncio.current_task(loop))

    unfinished_tasks = [t for t in tasks if not t.done()]

    if len(unfinished_tasks) != 0:
        logger.debug('Unfinished tasks: %s', unfinished_tasks)

        # Cancel unfinished tasks
        logger.info('Cancelling unfinished tasks')

        for _task in unfinished_tasks:
            pass #task.cancel()

    await asyncio.gather(*tasks, return_exceptions=True)

    # Stop event loop
    logger.info('Stopping event loop')
    loop.stop()
