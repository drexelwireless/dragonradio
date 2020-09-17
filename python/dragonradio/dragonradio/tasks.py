"""Utilities for managing tasks"""
import asyncio
import io
import logging

async def stopTasks(tasks, logger=logging.getLogger()):
    """Cancel tasks and wait for them to finish"""
    for task in tasks:
        task.cancel()

    results = await asyncio.gather(*tasks, return_exceptions=True)

    for task in tasks:
        if task.exception() is not None:
            f = io.StringIO()
            task.print_stack(file=f)
            logger.error(f.getvalue())

    return results
