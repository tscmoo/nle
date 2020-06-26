import os
import time
import timeit
import random

import pynle


def main():
    # MORE + compass directions + long compass directions.
    ACTIONS = [
        13,
        107,
        108,
        106,
        104,
        117,
        110,
        98,
        121,
        75,
        76,
        74,
        72,
        85,
        78,
        66,
        89,
    ]

    os.environ["NETHACKOPTIONS"] = "nolegacy"

    nle = pynle.NLE()

    nle.step(ord("y"))
    nle.step(ord("y"))
    nle.step(ord("\n"))

    steps = 0
    start_time = timeit.default_timer()
    start_steps = steps

    for episode in range(100):
        while not nle.done():
            ch = random.choice(ACTIONS)
            nle.step(ch)

            steps += 1

            if steps % 1000 == 0:
                end_time = timeit.default_timer()
                print("%f SPS" % ((steps - start_steps) / (end_time - start_time)))
                start_time = end_time
                start_steps = steps
        print("Finished episode %i after %i steps." % (episode + 1, steps))
        nle.reset()

    print("Finished after %i steps." % steps)


main()
