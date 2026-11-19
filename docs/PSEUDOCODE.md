# MCTS slice search: pseudocode

Pseudocode for `src/foresighter/implementation/algorithms/top_down_mcts_final_fixedroot.hpp`.

**Notation:** a *state* is an abstract AFTA state, and a *transition* is an abstract
hyperedge `head <- symbol(tails...)`. `pathHash` is the context hash of the transitions
chosen on the way down from the root, so a shared state reached through different
parents keeps separate statistics.

```text
globals:
  stats[transition][pathHash] = (totalReward, numVisits)
  rootStats[rootState]        = (totalReward, numVisits)
  pool[merkleHash]            = list of (concreteState, term)   // never cleared
  rewardCache[rootMerkleHash] = reward                          // never cleared

MCTS(slice, benchmark):
  while time remains:
    root   <- unvisited final state chosen at random, else argmax UCT over rootStats
    path, headToTransition, border, frontier <- SELECT(root)
    if border is empty:
      reward <- EXTRACT(CONCRETIZE(root, headToTransition))
    else:
      (t, h) <- uniform random element of border
      reward <- EXPAND_AND_SIMULATE(t, h, frontier, path, headToTransition)
    BACKPROPAGATE(path, reward); rootStats[root] += reward
  return fastest verified rewrite seen

SELECT(root):                                   // breadth-first over one derivation
  queue <- [(root, pathHash = 0)]; seen <- {root}
  while queue not empty:
    (s, h) <- pop front
    if some transition of s has stats[t][h].numVisits == 0:
      add every such (t, h) to border; add s to frontier
      continue
    t  <- BEST_UCT(s, h)
    h' <- combine(h, t)
    path += (t, h); headToTransition[s] <- t
    for each tail u of t not in seen: push (u, h'); seen += u

BEST_UCT(s, h):
  N <- sum of stats[t][h].numVisits over transitions t of s
  return argmax over t of  stats[t][h].mean + c * sqrt(ln N / stats[t][h].numVisits)
                                                            // c = sqrt(2)

EXPAND_AND_SIMULATE(t, h, frontier, path, headToTransition):
  path += (t, h); headToTransition[head(t)] <- t
  PLAYOUT(tails(t))
  for each other s in frontier: PLAYOUT([s])
  return EXTRACT(CONCRETIZE(root, headToTransition))

PLAYOUT(starts):                                // complete the skeleton at random
  for each unassigned state s reachable from starts, breadth-first:
    if s has transitions: headToTransition[s] <- uniform random transition of s

CONCRETIZE(root, headToTransition):             // memoized recursion, bottom-up values
  RESOLVE(s):
    if s already resolved this iteration: return its hash
    t <- headToTransition[s]
    hash <- combine(symbol(t), order(t))
    for each tail u of t:
      if u is parameter-like (column index, constant, input table):
        candidates[u] <- all concrete values of u; hash <- combine(hash, id(u))
      else:
        hu <- RESOLVE(u); candidates[u] <- pool[hu]; hash <- combine(hash, hu)
    if hash not in pool:
      pool[hash] <- [ (apply(t, combo), term) for combo in cartesian product of candidates
                      if apply(t, combo) is not Error or Bottom ]
    return hash
  return RESOLVE(root)

EXTRACT(rootHash):
  if rootHash in rewardCache: return rewardCache[rootHash]
  rewards <- []
  for each (state, term) in pool[rootHash] whose table matches the output example
      under some column mapping m:
    sql <- project(term, m)
    if CEGIS(sql) passes:
      r <- medianTime(original) / medianTime(sql) - 1
      rewards += r; update fastest rewrite
  rewardCache[rootHash] <- mean(rewards), or 0 if empty
  return it

BACKPROPAGATE(path, reward):
  for each (t, h) in path: stats[t][h].totalReward += reward; stats[t][h].numVisits += 1
```
