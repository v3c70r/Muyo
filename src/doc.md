# what to extract for global variables

## Immediate context

* Command Pool
* Graphics Queue
* Physical Deivce

# Brain dump

## Specular Cubemap

### Prefiltered cubemap

1. Generate a low-discrepancy random number pair v(x, y), where x \in [0, 1]; y \in [0,1]. This should work to sample a point cross a hemisphare
2. Importance sampling GGX narrow this distribution to a `Specular Lob` with a GGX distribution function, here probably explains why https://schuttejoe.github.io/post/ggximportancesamplingpart1/
ps: GGX describes rate of subfacet facing the half angle, given half angle H and normal N
3. 
