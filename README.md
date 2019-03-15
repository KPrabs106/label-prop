# Label Propagation 

Pointer chasing workload

Usage: ./labelprop -t NUM_THREADS -s PROBLEM_SIZE

## Algorithm
Node n has neighbors n<sub>1</sub>,n<sub>2</sub>,...,n<sub>k</sub>
At each time step t, every node updates its label to the most common label among its neighbors at the previous time step.
Stop when node has the same label as the maximum of its neighbors.

U. Raghavan, R. Albert, S. Kumara, Near linear time algorithm to detect community structures in large-scale networks. 
