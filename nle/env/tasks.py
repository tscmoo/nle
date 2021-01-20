# Copyright (c) Facebook, Inc. and its affiliates.
import enum

import numpy as np

from nle.env import base
from nle import nethack


TASK_ACTIONS = tuple(
    [nethack.MiscAction.MORE]
    + list(nethack.CompassDirection)
    + list(nethack.CompassDirectionLonger)
    + list(nethack.MiscDirection)
    + [nethack.Command.KICK, nethack.Command.EAT, nethack.Command.SEARCH]
)

TASK_ACTIONS = nethack.USEFUL_ACTIONS

class NetHackScore(base.NLE):
    """Environment for "score" task.

    The task is an augmentation of the standard NLE task. The return function is
    defined as:
    :math:`\text{score}_t - \text{score}_{t-1} + \text{TP}`,
    where the :math:`\text{TP}` is a time penalty that grows with the amount of
    environment steps that do not change the state (such as navigating menus).

    Args:
        penalty_mode (str): name of the mode for calculating the time step
            penalty. Can be ``constant``, ``exp``, ``square``, ``linear``, or
            ``always``. Defaults to ``constant``.
        penalty_step (float): constant applied to amount of frozen steps.
            Defaults to -0.01.
        penalty_time (float): constant applied to amount of frozen steps.
            Defaults to -0.0.

    """

    def __init__(
        self,
        *args,
        penalty_mode="constant",
        penalty_step: float = -0.01,
        penalty_time: float = -0.0,
        **kwargs,
    ):
        self.penalty_mode = penalty_mode
        self.penalty_step = penalty_step
        self.penalty_time = penalty_time

        self._frozen_steps = 0

        actions = kwargs.pop("actions", TASK_ACTIONS)
        super().__init__(*args, actions=actions, **kwargs)

    def _get_time_penalty(self, last_observation, observation):
        blstats_old = last_observation[self._blstats_index]
        blstats_new = observation[self._blstats_index]

        old_time = blstats_old[20]  # moves
        new_time = blstats_new[20]  # moves

        if old_time == new_time:
            self._frozen_steps += 1
        else:
            self._frozen_steps = 0

        penalty = 0
        if self.penalty_mode == "constant":
            if self._frozen_steps > 0:
                penalty += self.penalty_step
        elif self.penalty_mode == "exp":
            penalty += 2 ** self._frozen_steps * self.penalty_step
        elif self.penalty_mode == "square":
            penalty += self._frozen_steps ** 2 * self.penalty_step
        elif self.penalty_mode == "linear":
            penalty += self._frozen_steps * self.penalty_step
        elif self.penalty_mode == "always":
            penalty += self.penalty_step
        else:  # default
            raise ValueError("Unknown penalty_mode '%s'" % self.penalty_mode)
        penalty += (new_time - old_time) * self.penalty_time
        return penalty

    def _reward_fn(self, last_observation, observation, end_status):
        """Score delta, but with added a state loop penalty."""
        score_diff = super()._reward_fn(last_observation, observation, end_status)
        time_penalty = self._get_time_penalty(last_observation, observation)
        return score_diff + time_penalty


class NetHackStaircase(NetHackScore):
    """Environment for "staircase" task.

    This task requires the agent to get on top of a staircase down (>).
    The reward function is :math:`I + \text{TP}`, where :math:`I` is 1 if the
    task is successful, and 0 otherwise, and :math:`\text{TP}` is the time step
    function as defined by `NetHackScore`.
    """

    class StepStatus(enum.IntEnum):
        ABORTED = -1
        RUNNING = 0
        DEATH = 1
        TASK_SUCCESSFUL = 2

    def _is_episode_end(self, observation):
        internal = observation[self._internal_index]
        stairs_down = internal[4]
        if stairs_down:
            return self.StepStatus.TASK_SUCCESSFUL
        return self.StepStatus.RUNNING

    def _reward_fn(self, last_observation, observation, end_status):
        time_penalty = self._get_time_penalty(last_observation, observation)
        if end_status == self.StepStatus.TASK_SUCCESSFUL:
            reward = 1
        else:
            reward = 0
        return reward + time_penalty


class NetHackStaircasePet(NetHackStaircase):
    """Environment for "staircase-pet" task.

    This task requires the agent to get on top of a staircase down (>), while
    having their pet next to it. See `NetHackStaircase` for the reward function.
    """

    def _is_episode_end(self, observation):
        internal = observation[self._internal_index]
        stairs_down = internal[4]
        if stairs_down:
            glyphs = observation[self._glyph_index]
            blstats = observation[self._blstats_index]
            x, y = blstats[:2]

            neighbors = glyphs[y - 1 : y + 2, x - 1 : x + 2].reshape(-1).tolist()
            # TODO: vectorize
            for glyph in neighbors:
                if nethack.glyph_is_pet(glyph):
                    return self.StepStatus.TASK_SUCCESSFUL
        return self.StepStatus.RUNNING


class NetHackOracle(NetHackStaircase):
    """Environment for "oracle" task.

    This task requires the agent to reach the oracle (by standing next to it).
    See `NetHackStaircase` for the reward function.
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.oracle_glyph = None
        for glyph in range(nethack.GLYPH_MON_OFF, nethack.GLYPH_PET_OFF):
            if nethack.permonst(nethack.glyph_to_mon(glyph)).mname == "Oracle":
                self.oracle_glyph = glyph
                break
        assert self.oracle_glyph is not None

    def _is_episode_end(self, observation):
        glyphs = observation[self._glyph_index]
        blstats = observation[self._blstats_index]
        x, y = blstats[:2]

        neighbors = glyphs[y - 1 : y + 2, x - 1 : x + 2]
        if np.any(neighbors == self.oracle_glyph):
            return self.StepStatus.TASK_SUCCESSFUL
        return self.StepStatus.RUNNING


class NetHackGold(NetHackScore):
    """Environment for the "gold" task.

    The task is similar to the one defined by `NetHackScore`, but the reward
    uses changes in the amount of gold collected by the agent, rather than the
    score.

    The agent will pickup gold automatically by walking on top of it.
    """

    def __init__(self, *args, **kwargs):
        options = kwargs.pop("options", None)

        if options is None:
            # Copy & swap out "pickup_types".
            options = []
            for option in nethack.NETHACKOPTIONS:
                if option.startswith("pickup_types"):
                    options.append("pickup_types:$")
                    continue
                options.append(option)

        super().__init__(*args, options=options, **kwargs)

    def _reward_fn(self, last_observation, observation, end_status):
        """Difference between previous gold and new gold."""
        del end_status  # Unused
        if not self.env.in_normal_game():
            # Before game started or after it ended stats are zero.
            return 0.0

        old_blstats = last_observation[self._blstats_index]
        blstats = observation[self._blstats_index]

        old_gold = old_blstats[13]
        gold = blstats[13]

        time_penalty = self._get_time_penalty(last_observation, observation)

        return gold - old_gold + time_penalty


# FIXME: the way the reward function is currently structured means the
# agents gets a penalty of -1 every other step (since the
# uhunger increases by that)
# thus the step penalty becomes irrelevant
class NetHackEat(NetHackScore):
    """Environment for the "eat" task.

    The task is similar to the one defined by `NetHackScore`, but the reward
    uses positive changes in the character's hunger level (e.g. by consuming
    comestibles or monster corpses), rather than the score.
    """

    def _reward_fn(self, last_observation, observation, end_status):
        """Difference between previous hunger and new hunger."""
        del end_status  # Unused

        if not self.env.in_normal_game():
            # Before game started or after it ended stats are zero.
            return 0.0

        old_internal = last_observation[self._internal_index]
        internal = observation[self._internal_index]

        old_uhunger = old_internal[7]
        uhunger = internal[7]

        reward = max(0, uhunger - old_uhunger)

        time_penalty = self._get_time_penalty(last_observation, observation)

        return reward + time_penalty


class NetHackScout(NetHackScore):
    """Environment for the "scout" task.

    The task is similar to the one defined by `NetHackScore`, but the score is
    defined by the changes in glyphs discovered by the agent.
    """

    def reset(self, *args, **kwargs):
        self.dungeon_explored = {}
        return super().reset(*args, **kwargs)

    def _reward_fn(self, last_observation, observation, end_status):
        del end_status  # Unused

        if not self.env.in_normal_game():
            # Before game started or after it ended stats are zero.
            return 0.0

        reward = 0
        glyphs = observation[self._glyph_index]
        blstats = observation[self._blstats_index]

        dungeon_num, dungeon_level = blstats[23:25]

        key = (dungeon_num, dungeon_level)
        explored = np.sum(glyphs != 0)
        explored_old = 0
        if key in self.dungeon_explored:
            explored_old = self.dungeon_explored[key]
        reward = explored - explored_old
        self.dungeon_explored[key] = explored
        time_penalty = self._get_time_penalty(last_observation, observation)
        return reward + time_penalty


moo_running_mean_score = 1
moo_prev_score = 0
class NetHackMoo(base.NLE):
    """Environment for "moo" task.
    """

    def __init__(
        self,
        *args,
        penalty_mode="constant",
        penalty_step: float = -0.01,
        penalty_time: float = -0.0,
        **kwargs,
    ):
        self._frozen_steps = 0
        self._prev_time = 0
        self.visited = {}
        self.dungeon_explored = {}
        self.totalexplored = 0
        self._prev_score = 0
        self.prev_step_score = 0

        actions = kwargs.pop("actions", TASK_ACTIONS)
        super().__init__(*args, actions=actions, **kwargs)


    def step(self, action):
        # add state counting to step function if desired
        obs, reward, done, info = super().step(action)

        blstats = obs["blstats"]
        time = blstats[20]

        score = blstats[9] + self.totalexplored
        if not self.env.in_normal_game():
            score = self._prev_score

        if time == self._prev_time:
            self._frozen_steps += 1
            #print("self._frozen_steps is now ", self._frozen_steps)
            #if self._frozen_steps >= 12:
            #    reward = -0.01
            #if self._frozen_steps >= 48:
            #    reward = -1
            if self._frozen_steps >= 48:
                self._quit_game(self.last_observation, done)
                #reward = -1
                done = True
        else:
            self._frozen_steps = 0
        self._prev_time = time
        self._prev_score = score

#        if time > 1:
#          done = True
#          reward = 1

        global moo_running_mean_score
        global moo_prev_score

        #print("score %g, prev score %g" % (score, self._prev_score))

        #score = blstats[9] + time + self.totalexplored
        #if score >= moo_running_mean_score * 2:
        #    done = True

        if done:
            #if score > moo_prev_score:
            #    reward = 1
            #elif score < moo_prev_score:
            #    reward = -1
            #else:
            #    reward = 0
            #reward = score / (moo_running_mean_score + 1e-4) - 1
            #if reward > 1:
            #  reward = 1
            moo_prev_score = score
            alpha = 0.999
            moo_running_mean_score = moo_running_mean_score * alpha + score * (1 - alpha)
            print("score %g, running mean %g" % (score, moo_running_mean_score))
        #else:
        #    reward = 0

        diff = score - self.prev_step_score
        self.prev_step_score = score

        reward += diff

#        reward = diff / (moo_running_mean_score + 1e-4)
#        if reward < -1:
#            reward = -1
#        elif reward > 1:
#            reward = 1

        return obs, reward, done, info

    def reset(self, wizkit_items=None):
        obs = super().reset(wizkit_items=wizkit_items)
        self._frozen_steps = 0
        self._prev_time = 0
        self.visited = {}
        self.dungeon_explored = {}
        self.totalexplored = 0
        self._prev_score = 0
        self.prev_step_score = 0
        self.updatereward(obs["blstats"], obs["glyphs"])
        return obs

    def _reward_fn(self, last_observation, observation, end_status):
        blstats = observation[self._blstats_index]
        glyphs = observation[self._glyph_index]
        return self.updatereward(blstats, glyphs)

    def updatereward(self, blstats, glyphs):
        reward = 0
        dungeon_num, dungeon_level = blstats[23:25]
        key = (dungeon_num, dungeon_level)
        if key not in self.visited:
            self.visited[key] = True
            reward += 1
        explored = np.sum(glyphs != 0)
        explored_old = 0
        if key in self.dungeon_explored:
            explored_old = self.dungeon_explored[key]
        reward = (explored - explored_old) / 100
        self.totalexplored += reward
        self.dungeon_explored[key] = explored
        return reward

moo_running_mean_score = 1
moo_prev_score = 0
class NetHackMoo2(base.NLE):
    """Environment for "moo" task.
    """

    def __init__(
        self,
        *args,
        penalty_mode="constant",
        penalty_step: float = -0.01,
        penalty_time: float = -0.0,
        **kwargs,
    ):
        self._frozen_steps = 0
        self._prev_time = 0
        self.visited = {}
        self.dungeon_explored = {}
        self.totalexplored = 0
        self._prev_score = 0
        self.prev_step_score = 0

        actions = kwargs.pop("actions", TASK_ACTIONS)
        super().__init__(*args, actions=actions, **kwargs)


    def step(self, action):
        # add state counting to step function if desired
        obs, reward, done, info = super().step(action)

        blstats = obs["blstats"]
        time = blstats[20]

        score = blstats[9] + self.totalexplored
        if not self.env.in_normal_game():
            score = self._prev_score

        if time == self._prev_time:
            self._frozen_steps += 1
            #print("self._frozen_steps is now ", self._frozen_steps)
            #if self._frozen_steps >= 12:
            #    reward = -0.01
            #if self._frozen_steps >= 48:
            #    reward = -1
            if self._frozen_steps >= 48:
                self._quit_game(self.last_observation, done)
                #reward = -1
                done = True
        else:
            self._frozen_steps = 0
        self._prev_time = time
        self._prev_score = score

#        if time > 1:
#          done = True
#          reward = 1

        global moo_running_mean_score
        global moo_prev_score

        #print("score %g, prev score %g" % (score, self._prev_score))

        #score = blstats[9] + time + self.totalexplored
        #if score >= moo_running_mean_score * 2:
        #    done = True

        if done:
#            if score > moo_prev_score:
#                reward = 1
#            elif score < moo_prev_score:
#                reward = -1
#            else:
#                reward = 0
            reward = score / (moo_running_mean_score + 1e-4) - 1
            #if reward > 1:
            #  reward = 1
            #print("prev score %g\n" % moo_prev_score)
            moo_prev_score = score
            alpha = 0.999
            moo_running_mean_score = moo_running_mean_score * alpha + score * (1 - alpha)
            print("score %g, running mean %g" % (score, moo_running_mean_score))
        else:
            reward = 0

        diff = score - self.prev_step_score
        self.prev_step_score = score

        #reward += diff

#        reward = diff / (moo_running_mean_score + 1e-4)
#        if reward < -1:
#            reward = -1
#        elif reward > 1:
#            reward = 1

        return obs, reward, done, info

    def reset(self, wizkit_items=None):
        obs = super().reset(wizkit_items=wizkit_items)
        self._frozen_steps = 0
        self._prev_time = 0
        self.visited = {}
        self.dungeon_explored = {}
        self.totalexplored = 0
        self._prev_score = 0
        self.prev_step_score = 0
        self.updatereward(obs["blstats"], obs["glyphs"])
        return obs

    def _reward_fn(self, last_observation, observation, end_status):
        blstats = observation[self._blstats_index]
        glyphs = observation[self._glyph_index]
        return self.updatereward(blstats, glyphs)

    def updatereward(self, blstats, glyphs):
        reward = 0
        dungeon_num, dungeon_level = blstats[23:25]
        key = (dungeon_num, dungeon_level)
        if key not in self.visited:
            self.visited[key] = True
            reward += 1
        explored = np.sum(glyphs != 0)
        explored_old = 0
        if key in self.dungeon_explored:
            explored_old = self.dungeon_explored[key]
        reward = (explored - explored_old) / 100
        self.totalexplored += reward
        self.dungeon_explored[key] = explored
        return reward


moo_running_mean_score = 1
moo_prev_score = 0
class NetHackMoo3(base.NLE):
    """Environment for "moo" task.
    """

    def __init__(
        self,
        *args,
        penalty_mode="constant",
        penalty_step: float = -0.01,
        penalty_time: float = -0.0,
        **kwargs,
    ):
        self._frozen_steps = 0
        self._prev_time = 0
        self._prev_score = 0
        self.prev_step_score = 0

        actions = kwargs.pop("actions", TASK_ACTIONS)
        super().__init__(*args, actions=actions, **kwargs)


    def step(self, action):
        # add state counting to step function if desired
        obs, reward, done, info = super().step(action)

        blstats = obs["blstats"]
        time = blstats[20]

        score = blstats[9]
        if not self.env.in_normal_game():
            score = self._prev_score

        if time == self._prev_time:
            self._frozen_steps += 1
            #print("self._frozen_steps is now ", self._frozen_steps)
            if self._frozen_steps >= 12:
                reward = -0.01
            #if self._frozen_steps >= 48:
            #    reward = -1
            if self._frozen_steps >= 48:
                self._quit_game(self.last_observation, done)
                reward = -1
                done = True
        else:
            self._frozen_steps = 0
        self._prev_time = time
        self._prev_score = score

#        if time > 1:
#          done = True
#          reward = 1

        global moo_running_mean_score
        global moo_prev_score

        #print("score %g, prev score %g" % (score, self._prev_score))

        #score = blstats[9] + time + self.totalexplored
        #if score >= moo_running_mean_score * 2:
        #    done = True

        if done:
            #if score > moo_prev_score:
            #    reward = 1
            #elif score < moo_prev_score:
            #    reward = -1
            #else:
            #    reward = 0
            #reward = score / (moo_running_mean_score + 1e-4) - 1
            #if reward > 1:
            #  reward = 1
            #print("prev score %g\n" % moo_prev_score)
            moo_prev_score = score
            alpha = 0.99
            moo_running_mean_score = moo_running_mean_score * alpha + score * (1 - alpha)
            print("score %g, running mean %g" % (score, moo_running_mean_score))
        #else:
        #    reward = 0

        diff = score - self.prev_step_score
        self.prev_step_score = score

        reward += diff

#        reward = diff / (moo_running_mean_score + 1e-4)
#        if reward < -1:
#            reward = -1
#        elif reward > 1:
#            reward = 1

        return obs, reward, done, info

    def reset(self, wizkit_items=None):
        obs = super().reset(wizkit_items=wizkit_items)
        self._frozen_steps = 0
        self._prev_time = 0
        self.totalexplored = 0
        self._prev_score = 0
        self.prev_step_score = 0
        return obs

    def _reward_fn(self, last_observation, observation, end_status):
        return 0

class NetHackDescend(NetHackScore):
    """Environment for "descend" task.
    """

    def reset(self, *args, **kwargs):
        obs = super().reset(*args, **kwargs)
        self.visited = {}
        self.dungeon_explored = {}
        self.visitcount = 0
        self.updatereward(obs["blstats"], obs["glyphs"])
        return obs

    def _reward_fn(self, last_observation, observation, end_status):
        blstats = observation[self._blstats_index]
        glyphs = observation[self._glyph_index]
        internal = observation[self._internal_index]
        if internal[3]:  # xwaitforspace
            return self._get_time_penalty(last_observation, observation)
        return self.updatereward(blstats, glyphs) + self._get_time_penalty(last_observation, observation)

    def updatereward(self, blstats, glyphs):
        reward = 0
        dungeon_num, dungeon_level = blstats[23:25]
        key = (dungeon_num, dungeon_level)
        if key not in self.visited:
            self.visited[key] = True
            self.visitcount += 1
            reward += 10
        explored = np.sum(glyphs != 0)
        explored_old = 0
        if key in self.dungeon_explored:
            explored_old = self.dungeon_explored[key]
        reward += (explored - explored_old) / (200 * self.visitcount*self.visitcount)
        self.dungeon_explored[key] = explored
        return reward


class NetHackDescend2(NetHackScore):
    """Environment for "descend" task.
    """

    def reset(self, *args, **kwargs):
        obs = super().reset(*args, **kwargs)
        self.visited = {}
        self.dungeon_explored = {}
        self.visitcount = 0
        self._prev_time = 0
        self.poshistory = []
        self.updatereward(obs["blstats"], obs["glyphs"])
        return obs

    def step(self, action):
        obs, reward, done, info = super().step(action)

        blstats = obs["blstats"]
        time = blstats[20]

        if time == self._prev_time:
            self._frozen_steps += 1
            #print("self._frozen_steps is now ", self._frozen_steps)
            reward += 0.001
            if self._frozen_steps >= 12:
                reward += -0.01
            #if self._frozen_steps >= 48:
            #    reward += -1
            if self._frozen_steps >= 48:
                self._quit_game(self.last_observation, done)
                #reward += -1
                done = True
        else:
            self._frozen_steps = 0
        self._prev_time = time

        if done:
            reward += -0.25

        return obs, reward, done, info


    def _reward_fn(self, last_observation, observation, end_status):
        blstats = observation[self._blstats_index]
        last_blstats = last_observation[self._blstats_index]
        glyphs = observation[self._glyph_index]
        internal = observation[self._internal_index]
        reward = 0
        if not internal[3]:  # xwaitforspace
            reward += self.updatereward(blstats, glyphs)
        pos = (blstats[23], blstats[24], int(blstats[0] / 3), int(blstats[1] / 3))
        if not pos in self.poshistory:
            reward += 0.01
        if len(self.poshistory) > 24:
            self.poshistory.pop(0)
        self.poshistory.append(pos)
        if blstats[19] > last_blstats[19]: # exp
            reward += 0.04
        if blstats[18] > last_blstats[18]: # exp level
            reward += 0.25
        return reward
        #if internal[3]:  # xwaitforspace
        #    return self._get_time_penalty(last_observation, observation)
        #return self.updatereward(blstats, glyphs) + self._get_time_penalty(last_observation, observation)

    def updatereward(self, blstats, glyphs):
        reward = 0
        dungeon_num, dungeon_level = blstats[23:25]
        key = (dungeon_num, dungeon_level)
        if key not in self.visited:
            self.visited[key] = True
            self.visitcount += 1
            reward += 1
        #explored = np.sum(glyphs != 0)
        #explored_old = 0
        #if key in self.dungeon_explored:
        #    explored_old = self.dungeon_explored[key]
        #reward += (explored - explored_old) / (200 * self.visitcount*self.visitcount)
        #self.dungeon_explored[key] = explored
        return reward

