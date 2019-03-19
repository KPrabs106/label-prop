#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>

#include <unordered_map>
#include <vector>

#define PROBABILITY 0.25

typedef struct Node {
    int id;
    int label;
    int num_neighbors;
    struct Node **neighbors;
} Node;

typedef struct Arg {
    Node *nodes;
    int num_nodes;
    pthread_barrier_t *compute_barrier;
    pthread_barrier_t *store_barrier;
    pthread_barrier_t *check_completion_barrier;
    pthread_barrier_t *check_finish;
    bool *reached_majority;
    bool *run;
} Arg;

void parse_args(int *num_threads, int *num_nodes, int argc, char **argv){
    int c;

    extern int opterr;
    extern int optopt;
    extern char *optarg;

    opterr = 0;

    while( (c = getopt(argc, argv, "t:s:")) != -1){
        switch (c){
            case 't':
                *num_threads = atoi(optarg);
                break;
            case 's':
                *num_nodes = atoi(optarg);
                break;
            case '?':
                if (optopt == 't' || optopt == 's')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr,
                            "Unknown option character `\\x%x'.\n",
                            optopt);
                abort();
            default:
                abort();
        }
    }
}

double get_random_number(){
    static bool seed = false;
    if(!seed){
        srand(time(NULL));
        seed = true;
    }
    return (double)rand() / (double)RAND_MAX;
}

void increase_neighbors(Node *node){
    node->num_neighbors++;

    if(node->num_neighbors == 1){
        node->neighbors = (Node**) malloc(sizeof(Node*));
    }
    else{
        node->neighbors = (Node**) realloc(node->neighbors, sizeof(Node*) * node->num_neighbors);
    }
}

void link_nodes(Node *a, Node *b){
    increase_neighbors(a);
    increase_neighbors(b);

    a->neighbors[a->num_neighbors-1] = b;
    b->neighbors[b->num_neighbors-1] = a;
}

void initialize_nodes(Node *nodes, int num_nodes){
    for(int i = 0; i < num_nodes; i++){
        nodes[i].id = i;
        nodes[i].label = i;
        nodes[i].neighbors = 0;
    }

    // Erdos-Renyi model
    for(int i = 0; i < num_nodes - 1; i++){
        for(int j = i+1; j < num_nodes; j++){
            if(get_random_number() < PROBABILITY){ // create link between i and j only if random_number is less than PROBABILITY 
                link_nodes(&nodes[i], &nodes[j]);
                printf("%d <-> %d\n", i, j);
            }
        }        
    }
}

int get_majority_label(Node node){
    if(node.num_neighbors == 0){
        return node.label;
    }
    std::unordered_map<int, int> label_counts;
    printf("counting neighbor labels...\n");
    for(int i = 0; i < node.num_neighbors; i++){
        label_counts[node.neighbors[i]->label]++;    
    }

    std::vector<int> max_labels;

    int current_max = 0;

    for(auto it = label_counts.cbegin(); it != label_counts.cend(); ++it){
        if(it->second > current_max){
            max_labels.clear();
            max_labels.push_back(it->first);
            current_max = it->second;
        }
        if(it->second == current_max){
            max_labels.push_back(it->first);
        }
    }
    if(max_labels.size() == 1){
        return max_labels[0];
    }
    return max_labels[rand() % max_labels.size()]; // uniform breaking of times
}

void *label_prop(void *thread_arg){
    Arg *arg = (Arg *)thread_arg;
    int *next_labels = (int*) malloc(sizeof(int)*arg->num_nodes);
    int *last_labels = (int*) calloc(arg->num_nodes, sizeof(int));
    printf("thread started with %d nodes\n", arg->num_nodes);
    
    while(*(arg->run)){
        printf("waiting at barrier...\n");
        pthread_barrier_wait(arg->compute_barrier);
        
        printf("Finding majority label...\n");
        for(int i = 0; i < arg->num_nodes; i++){
            next_labels[i] = get_majority_label(arg->nodes[i]);
        }

        bool reached_majority = true;
        for(int i = 0; i < arg->num_nodes; i++){
            if(next_labels[i] != last_labels[i]){
                reached_majority = false;
                break;
            }
        }
        

        pthread_barrier_wait(arg->store_barrier);

        printf("Writing majority label...\n");
        for(int i = 0; i < arg->num_nodes; i++){
            last_labels[i] = arg->nodes[i].label;
            arg->nodes[i].label = next_labels[i];
        }

        pthread_barrier_wait(arg->check_completion_barrier);
        
        
        // for(int i = 0; i < arg->num_nodes; i++){
        //     if(arg->nodes[i].label != get_majority_label(arg->nodes[i])){
        //         reached_majority = false;
        //         break;
        //     }
        // }

        printf("Have reached majority :%d\n", reached_majority);
        *arg->reached_majority = reached_majority;

        pthread_barrier_wait(arg->check_finish);
    }
    printf("finished...\n");
    pthread_exit(NULL);
    return NULL;
}

void split_work(Arg* thread_args, int num_threads, Node* nodes, int num_nodes){
    int min_nodes_per_thread = num_nodes / num_threads;
    int extra_nodes = num_nodes % num_threads;
    int current_base_node = 0;

    for(int i = 0; i < num_threads; i++){
        thread_args[i].num_nodes = min_nodes_per_thread;
        
        if(extra_nodes > 0){
            thread_args[i].num_nodes++;
            extra_nodes--;
        }
        
        thread_args[i].nodes = &nodes[current_base_node];
        current_base_node += thread_args[i].num_nodes;
    }
}

void init_barriers(Arg *thread_args, int num_threads, pthread_barrier_t **master_completion_barrier, pthread_barrier_t **master_finish_barrier, bool *run){
    pthread_barrier_t *compute_barrier = (pthread_barrier_t*) malloc(sizeof(pthread_barrier_t));
    pthread_barrier_init(compute_barrier, NULL, num_threads);
    pthread_barrier_t *store_barrier = (pthread_barrier_t*) malloc(sizeof(pthread_barrier_t));
    pthread_barrier_init(store_barrier, NULL, num_threads);
    pthread_barrier_t *check_completion_barrier = (pthread_barrier_t*) malloc(sizeof(pthread_barrier_t));
    pthread_barrier_init(check_completion_barrier, NULL, num_threads+1);
    pthread_barrier_t *check_finish = (pthread_barrier_t*) malloc(sizeof(pthread_barrier_t));
    pthread_barrier_init(check_finish, NULL, num_threads+1);
    
    *master_completion_barrier = check_completion_barrier;
    *master_finish_barrier = check_finish;

    for(int i = 0; i < num_threads; i++){
        thread_args[i].compute_barrier = compute_barrier;
        thread_args[i].store_barrier = store_barrier;
        thread_args[i].check_completion_barrier = check_completion_barrier;
        thread_args[i].check_finish = check_finish;
        thread_args[i].reached_majority = (bool*) malloc(sizeof(bool));
        thread_args[i].run = run;
    }
}

void spawn_threads(int num_threads, Node *nodes, int num_nodes){
    Arg* thread_args = (Arg*) malloc(sizeof(Arg) * num_threads);
    split_work(thread_args, num_threads, nodes, num_nodes );

    pthread_barrier_t *check_completion_barrier;
    pthread_barrier_t *check_finish;
    bool run = true;
    init_barriers(thread_args, num_threads, &check_completion_barrier, &check_finish, &run);

    pthread_t *threads = (pthread_t*) calloc(num_threads, sizeof(pthread_t));
    for(int i = 0; i < num_threads; i++){
        pthread_create(&threads[i], NULL, &label_prop, (void*)&thread_args[i]);
    }


    while(run){
        printf("Master is waiting for an update...\n");
        pthread_barrier_wait(check_completion_barrier);
        // pthread_barrier_init(check_finish, NULL, num_threads+1);

        printf("Current state of nodes:\n");
        for(int i = 0; i < num_nodes; i++){
            printf("\tlabel(%d) = %d\n", i, nodes[i].label);
        }

        bool continue_run = false;
        for(int i = 0; i < num_threads; i++){ // check if all threads have reached a majority
            if( *(thread_args[i].reached_majority) == false ){
                continue_run = true;
                break;
            }
        }
        run = continue_run;
        
        printf("Master says to continue: %d\n", run);
        
        pthread_barrier_wait(check_finish);
        // pthread_barrier_init(check_completion_barrier, NULL, num_threads+1);
        
    }


    printf("joining threads\n");
    for(int i = 0; i < num_threads; i++){
        pthread_join(threads[i], NULL);
    }

}

int main(int argc, char **argv){
    int num_threads = 0;
    int num_nodes = 0;
    
    parse_args(&num_threads, &num_nodes, argc, argv);

    printf("Starting label propagation with %d threads and %d nodes\n", num_threads, num_nodes);

    Node *nodes = (Node*) malloc(sizeof(Node)*num_nodes);
    initialize_nodes(nodes, num_nodes);

    spawn_threads(num_threads, nodes, num_nodes);
}