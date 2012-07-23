
#include "thread.h"
#include "trans.h"

namespace arghmm {


// assert that a thread is compatiable with an ARG
bool assert_trees_thread(LocalTrees *trees, int *thread_path, int ntimes)
{
    LocalTree *last_tree = NULL;
    States states1, states2;
    States *states = &states1;
    States *last_states = &states2;

    // loop through blocks
    int start = trees->start_coord;
    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it) {
        get_coal_states(it->tree, ntimes, *states);

        // check spr
        if (last_tree) {
            assert(assert_spr(last_tree, it->tree, &it->spr, it->mapping));

            int determ[last_states->size()];
            get_deterministic_transitions(
                 it->tree, last_tree, it->spr, it->mapping,
                 *last_states, *states, ntimes, determ);

            int a = thread_path[start-1];
            int b = determ[thread_path[start-1]];
            int c = thread_path[start];

            printf(">> %d (%d,%d) --> (%d,%d) == (%d,%d)\n", start, 
                   (*last_states)[a].node, (*last_states)[a].time, 
                   (*states)[b].node, (*states)[b].time,
                   (*states)[c].node, (*states)[c].time);
        }
        
        // set last tree and state pointers
        last_tree = it->tree;
        last_states = states;
        if (states == &states1)
            states = &states2;
        else
            states = &states1;

        start += it->blocklen;
    }

    return true;
}


// rename a node from src_node to dest_node while mantaining the 
// structure of the tree
void displace_node(LocalTree *tree, int src_node, int dest_node)
{
    // special case
    if (src_node == dest_node)
        return;

    LocalNode *nodes = tree->nodes;

    // copy displaced node
    nodes[dest_node].copy(nodes[src_node]);

    // notify parent of displacement
    int parent = nodes[dest_node].parent;
    if (parent != -1) {
        int *c = nodes[parent].child;
        if (c[0] == src_node)
            c[0] = dest_node;
        else
            c[1] = dest_node;
    }

    // notify children of displacement
    int *c = nodes[dest_node].child;
    if (c[0] != -1)
        nodes[c[0]].parent = dest_node;
    if (c[1] != -1)
        nodes[c[1]].parent = dest_node;
}


// Add a single new leaf to a tree.  
// Leaf connects to the point indicated by (node, time)
// 
//   - New leaf will be named 'nleaves' (newleaf)
//   - Node currently named 'nleaves' will be displaced to 'nnodes' (displaced)
//   - New internal node will be named 'nnodes+1' (newcoal)
//
void add_tree_branch(LocalTree *tree, int node, int time)
{
    // get tree info
    LocalNode *nodes = tree->nodes;
    int nleaves = tree->get_num_leaves();
    int nnodes = tree->nnodes;
    int nnodes2 = nnodes + 2;
    
    // get major node ids
    int newleaf = nleaves;
    int displaced = nnodes;
    int newcoal = nnodes + 1;
    
    // determine node displacement
    int node2 = (node != newleaf ? node : displaced);
    int parent = nodes[node].parent;
    int parent2 = (parent != newleaf ? parent : displaced);

    // displace node
    if (newleaf < displaced)
        displace_node(tree, newleaf, displaced);

    // add new leaf
    nodes[newleaf].parent = newcoal;
    nodes[newleaf].child[0] = -1;
    nodes[newleaf].child[1] = -1;
    nodes[newleaf].age = 0;
        
    // add new coal node
    nodes[newcoal].parent = parent2;
    nodes[newcoal].child[0] = newleaf;
    nodes[newcoal].child[1] = node2;
    nodes[newcoal].age = time;

    // fix pointers
    nodes[node2].parent = newcoal;
    if (parent2 != -1) {
        int *c = nodes[parent2].child;
        if (c[0] == node2)
            c[0] = newcoal;
        else
            c[1] = newcoal;
    }

    // fix up tree data
    tree->nnodes = nnodes2;
    if (nodes[newcoal].parent == -1)
        tree->root = newcoal;
    else
        tree->root = (tree->root != newleaf ? tree->root : displaced);
}


// removes a leaf branch from a local tree
// any node displacements are recorded in the displace array
// If displace is NULL, displacements are not recorded
void remove_tree_branch(LocalTree *tree, int remove_leaf, int *displace)
{
    LocalNode *nodes = tree->nodes;
    int nnodes = tree->nnodes;
    int last_leaf = tree->get_num_leaves() - 1;

    // remove coal node
    int remove_coal = nodes[remove_leaf].parent;
    int *c = nodes[remove_coal].child;
    int coal_child = (c[0] == remove_leaf ? c[1] : c[0]);
    int coal_parent = nodes[remove_coal].parent;
    nodes[coal_child].parent = coal_parent;
    if (coal_parent != -1) {
        c = nodes[coal_parent].child;
        if (c[0] == remove_coal)
            c[0] = coal_child;
        else
            c[1] = coal_child;
    }
        
    // record displace nodes
    if (displace) {
        for (int i=0; i<nnodes; i++)
            displace[i] = i;
        displace[remove_leaf] = -1;
        displace[remove_coal] = -1;
    }
        
    // move last leaf into remove_leaf spot
    if (last_leaf != remove_leaf) {
        if (displace)
            displace[last_leaf] = remove_leaf;
        displace_node(tree, last_leaf, remove_leaf);
    }

    // move nodes in nnodes-2 and nnodes-1 into holes
    int hole = last_leaf;
    if (remove_coal != nnodes-2) {
        if (displace)
            displace[nnodes-2] = hole;
        displace_node(tree, nnodes-2, hole);
        hole = remove_coal;
    }
    if (remove_coal != nnodes-1) {
        if (displace)
            displace[nnodes-1] = hole;
        displace_node(tree, nnodes-1, hole);
    }
    
    // set tree data
    tree->nnodes -= 2;
    int root = tree->root;
    if (tree->root == remove_coal)
        root = coal_child;
    if (root == nnodes-2)
        root = last_leaf;
    if (root == nnodes-1)
        root = hole;
    tree->root = root;
}



// update an SPR and mapping after adding a new branch to two
// neighboring local trees
void add_spr_branch(LocalTree *tree, LocalTree *last_tree, 
                    State state, State last_state,
                    Spr *spr, int *mapping,
                    int newleaf, int displaced, int newcoal)
{
    // get tree info
    LocalNode *nodes = tree->nodes;
    LocalNode *last_nodes = last_tree->nodes;

    // determine node displacement
    int node2 = (state.node != newleaf ? state.node : displaced);


    // update mapping due to displacement
    mapping[displaced] = mapping[newleaf];
    mapping[newleaf] = newleaf;
            
    // set default new node mapping 
    mapping[newcoal] = newcoal;
        
    for (int i=newleaf+1; i<tree->nnodes; i++) {
        if (mapping[i] == newleaf)
            mapping[i] = displaced;
    }


    // update spr due to displacement
    if (spr->recomb_node == newleaf)
        spr->recomb_node = displaced;
    if (spr->coal_node == newleaf)
        spr->coal_node = displaced;

        
    // parent of recomb node should be the recoal point
    // however, if it equals newcoal, then either (1) the recomb branch is 
    // renamed, (2) there is mediation, or (3) new branch escapes
    int recoal = nodes[mapping[spr->recomb_node]].parent;
    if (recoal == newcoal) {
        if (mapping[last_state.node] == node2) {
            // (1) recomb is above coal state, we rename spr recomb node
            spr->recomb_node = newcoal;
        } else {
            // if this is a mediated coal, then state should equal recomb
            int state_node = (state.node != newleaf) ? 
                state.node : displaced;
            if (state_node == mapping[spr->recomb_node]) {
                // (3) this is a mediated coal, rename coal node and time
                spr->coal_node = newleaf;
                spr->coal_time = state.time;
            } else {
                // (2) this is the new branch escaping
                // no other updates are necessary
            }
        }
    } else {
        // the other possibility is that newcoal is under recoal point
        // if newcoal is child of recoal, then coal is renamed
        int *c = nodes[recoal].child;
        if (c[0] == newcoal || c[1] == newcoal) {
            // we either coal above the newcoal or our existing
            // node just broke and newcoal was underneath.
                
            // if newcoal was previously above spr->coal_node
            // then we rename the spr coal node
            if (last_nodes[spr->coal_node].parent == newcoal)
                spr->coal_node = newcoal;
        }
    }
            
    // determine if mapping of new node needs to be changed
    // newcoal was parent of recomb, it is broken
    if (last_nodes[spr->recomb_node].parent == newcoal) {
        mapping[newcoal] = -1;
        int p = last_nodes[newcoal].parent;
        if (p != -1)
            mapping[p] = newcoal;
    } else {
        // newcoal was not broken
        // find child without recomb or coal on it
        int x = newcoal;
        while (true) {
            int y = last_nodes[x].child[0];
            if (y == spr->coal_node || y == spr->recomb_node)
                y = last_nodes[x].child[1];
            x = y;
            if (mapping[x] != -1)
                break;
        }
        mapping[newcoal] = nodes[mapping[x]].parent;
    }
}




// add a leaf thread to an ARG
void add_arg_thread(LocalTrees *trees, int ntimes, int *thread_path, int seqid,
                    vector<int> &recomb_pos, vector<NodePoint> &recombs)
{
    unsigned int irecomb = 0;
    int nleaves = trees->get_num_leaves();
    int nnodes = trees->nnodes;
    int nnodes2 = nnodes + 2;
    
    // node names
    int newleaf = nleaves;
    int displaced = nnodes;
    int newcoal = nnodes + 1;

    States states;
    State last_state;
    LocalTree *last_tree = NULL;


    // update trees info
    trees->seqids.push_back(seqid);
    trees->nnodes = nnodes2;
    

    // loop through blocks
    int end = trees->start_coord;
    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it) {
        LocalTree *tree = it->tree;
        Spr *spr = &(it->spr);
        int start = end;
        end += it->blocklen;
        get_coal_states(tree, ntimes, states);
        
        // add new branch to local tree
        it->ensure_capacity(nnodes2);
        State state = states[thread_path[start]];
        add_tree_branch(tree, state.node, state.time);
        
        // update mapping and spr
        int *mapping = it->mapping;
        if (mapping) {
            add_spr_branch(tree, last_tree, state, last_state,
                           &it->spr, mapping,
                           newleaf, displaced, newcoal);
            assert(assert_spr(last_tree, tree, spr, mapping));
        }

        // assert new branch is where it should be
        assert(tree->nodes[newcoal].age == states[thread_path[start]].time);


        // break this block for each new recomb within this block
        for (;irecomb < recombs.size() && 
              recomb_pos[irecomb] < end; irecomb++) {
            int pos = recomb_pos[irecomb];
            LocalNode *nodes = tree->nodes;
            state = states[thread_path[pos]];
            last_state = states[thread_path[pos-1]];
            
            // assert that thread time is still on track
            assert(tree->nodes[newcoal].age == last_state.time);

            // determine real name of recomb node
            // it may be different due to adding a new branch
            Spr spr2;
            spr2.recomb_node = recombs[irecomb].node;
            spr2.recomb_time = recombs[irecomb].time;
            if (spr2.recomb_node == newleaf)
                spr2.recomb_node = displaced;            
            assert(spr2.recomb_time <= tree->nodes[newcoal].age);

            // determine coal node and time
            //int istate = thread_path[pos];
            if (spr2.recomb_node == -1) {
                // recomb on new branch, coal given thread
                spr2.recomb_node = newleaf;
                spr2.coal_node = state.node;

                // fix coal node due to displacement
                if (spr2.coal_node == newleaf)
                    spr2.coal_node = displaced;

                // rename coal node due to newcoal underneath
                if (state.node == last_state.node &&
                    state.time > last_state.time)
                    spr2.coal_node = newcoal;

            } else {
                // recomb in ARG, coal on new branch
                if (state.time > last_state.time)
                    spr2.coal_node = nodes[newleaf].parent;
                else
                    spr2.coal_node = newleaf;
            }
            spr2.coal_time = state.time;

            
            // determine mapping:
            // all nodes keep their name expect the broken node, which is the
            // parent of recomb
            int *mapping2 = new int [tree->capacity];
            for (int j=0; j<nnodes2; j++)
                mapping2[j] = j;
            mapping2[nodes[spr2.recomb_node].parent] = -1;

            
            // make new local tree and apply SPR operation
            LocalTree *new_tree = new LocalTree(nnodes2, tree->capacity);
            new_tree->copy(*tree);
            apply_spr(new_tree, spr2);

            // calculate block end
            int block_end;
            if (irecomb < recombs.size() - 1)
                // use next recomb in this block to determine block end
                block_end = min(recomb_pos[irecomb+1], end);
            else
                // no more recombs in this block
                block_end = end;
           
            // insert new tree and spr into local trees list
            it->blocklen = pos - start;
            ++it;
            it = trees->trees.insert(it, 
                LocalTreeSpr(new_tree, spr2, block_end - pos, mapping2));
            

            // assert tree and SPR
            assert(assert_tree(new_tree));
            assert(new_tree->nodes[newcoal].age == state.time);
            assert(assert_spr(tree, new_tree, &spr2, mapping2));

            // remember the previous tree for next iteration of loop
            tree = new_tree;
            nodes = tree->nodes;
            start = pos;
        }

        // remember the previous tree for next iteration of loop
        last_tree = tree;
        last_state = states[thread_path[end-1]];
        if (last_state.node == newleaf)
            last_state.node = displaced;
    }

    assert_trees(trees);
}



// Removes a leaf thread from an ARG
// NOTE: if remove_leaf is not last_leaf, nleaves - 1, 
// last_leaf is renamed to remove_leaf
void remove_arg_thread(LocalTrees *trees, int remove_seqid)
{
    int nnodes = trees->nnodes;
    int nleaves = trees->get_num_leaves();
    int displace[nnodes];
    int last_leaf = nleaves - 1;

    // find leaf to remove from seqid
    int remove_leaf = -1;
    for (unsigned int i=0; i<trees->seqids.size(); i++) {
        if (trees->seqids[i] == remove_seqid) {
            remove_leaf = i;
            break;
        }
    }
    assert(remove_leaf != -1);
    
    // special case for trunk genealogy
    if (nnodes == 3) {
        assert(remove_leaf == 0 || remove_leaf == 1);
        trees->make_trunk(trees->start_coord, trees->end_coord,
                          trees->begin()->tree->capacity);
        trees->seqids[0] = trees->seqids[1-remove_leaf];
        trees->seqids.resize(1);
        return;
    }

    
    // remove extra branch
    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it) {
        LocalTree *tree = it->tree;
        LocalNode *nodes = tree->nodes;
        
        // get information about removal
        int remove_coal = nodes[remove_leaf].parent;
        int coal_time = nodes[remove_coal].age;
        int *c = nodes[remove_coal].child;
        int coal_child = (c[0] == remove_leaf ? c[1] : c[0]);
        
        // remove branch from tree        
        remove_tree_branch(tree, remove_leaf, displace);
        //assert_tree(tree);
        
        
        // fix this mapping due to displacement
        int *mapping = it->mapping;
        if (mapping) {
            for (int i=0; i<nnodes-2; i++)
                if (mapping[i] != -1)
                    mapping[i] = displace[mapping[i]];
                else
                    mapping[i] = -1;
        }
        
        // get next tree
        LocalTrees::iterator it2 = it;
        ++it2;
        if (it2 == trees->end())
            continue;

        // fix next mapping due to displacement
        mapping = it2->mapping;
        if (displace[last_leaf] != -1)
            mapping[displace[last_leaf]] = mapping[last_leaf];
        if (displace[nnodes-2] != -1)
            mapping[displace[nnodes-2]] = mapping[nnodes-2];
        if (displace[nnodes-1] != -1)
            mapping[displace[nnodes-1]] = mapping[nnodes-1];
        
        
        
        // fix SPR
        Spr *spr = &it2->spr;
        
        // get new name of coal_child
        coal_child = displace[coal_child];

        // if recomb is on branch removed, prune it
        if (spr->recomb_node == remove_leaf) {
            spr->set_null();
            continue;
        }

        // see if recomb node is renamed
        if (spr->recomb_node == remove_coal) {
            spr->recomb_node = coal_child;
        } else {
            // rename recomb_node due to displacement
            spr->recomb_node = displace[spr->recomb_node];
        }
        
        // if recomb is on root branch, prune it
        if (spr->recomb_node == coal_child && nodes[coal_child].parent == -1) {
            spr->set_null();
            continue;
        }

        // rename spr coal_node
        if (spr->coal_node == remove_leaf) {
            // mediated coal
            spr->coal_node = coal_child;
            spr->coal_time = coal_time;

        } else if (spr->coal_node == remove_coal) {
            // move coal down a branch
            spr->coal_node = coal_child;
        } else {
            // rename recomb_node due to displacement
            spr->coal_node = displace[spr->coal_node];
        }


        // check for bubbles
        if (spr->recomb_node == spr->coal_node) {
            spr->set_null();
            continue;
        }
    }
    

    // update trees info
    trees->seqids[remove_leaf] = trees->seqids[last_leaf];
    trees->seqids.resize(nleaves - 1);
    trees->nnodes -= 2;
    
    // remove extra trees
    remove_null_sprs(trees);
    
    assert_trees(trees);
}


//=============================================================================
// internal branch threading operations


// find recoal node, it is the node with no inward mappings
int get_recoal_node(const LocalTree *tree, 
                    const Spr &spr, const int *mapping)
{
    const int nnodes = tree->nnodes;
    bool mapped[nnodes];
    fill(mapped, mapped + nnodes, false);

    for (int i=0; i<nnodes; i++)
        if (mapping[i] != -1)
            mapped[mapping[i]] = true;
    
    for (int i=0; i<nnodes; i++)
        if (!mapped[i])
            return i;

    assert(false);
    return -1;
}


// find the next possible branches in a removal path
void get_next_removal_nodes(const LocalTree *tree1, const LocalTree *tree2,
                            const Spr &spr2, const int *mapping2,
                            int node, int next_nodes[2])
{   
    const int recoal = get_recoal_node(tree1, spr2, mapping2);
    //const int recoal = tree2->nodes[mapping2[spr2.recomb_node]].parent;
    

    // get passive transition
    next_nodes[0] = mapping2[node];
    if (next_nodes[0] == -1) {
        // node is broken by SPR
        // next node is then non-recomb child or recoal
        int sib = tree1->get_sibling(spr2.recomb_node);
        if (spr2.coal_node == sib)
            next_nodes[0] = recoal;
        else
            next_nodes[0] = mapping2[sib];
    }
    
    // get possible active transition
    // if recoal is on this branch (node) then there is a split in the path
    if (spr2.coal_node == node) {
        // find recoal node, its the node with no inward mappings
        next_nodes[1] = recoal;
    } else {
        // no second transition
        next_nodes[1] = -1;
    }
}


// find the next possible branches in all possible removal path
void get_all_next_removal_nodes(const LocalTree *tree1, const LocalTree *tree2,
                                const Spr &spr2, const int *mapping2,
                                int next_nodes[][2])
{   
    const int recoal = get_recoal_node(tree1, spr2, mapping2);
    
    for (int node=0; node<tree1->nnodes; node++) {
        // get passive transition
        next_nodes[node][0] = mapping2[node];
        if (next_nodes[node][0] == -1) {
            // node is broken by SPR
            // next node is then non-recomb child or recoal
            int sib = tree1->get_sibling(spr2.recomb_node);
            if (spr2.coal_node == sib)
                next_nodes[node][0] = recoal;
            else
                next_nodes[node][0] = mapping2[sib];
        }
        
        // get possible active transition
        // if recoal is on this branch (node) then there is a split in the path
        if (spr2.coal_node == node) {
            // find recoal node, its the node with no inward mappings
            next_nodes[node][1] = recoal;
        } else {
            // no second transition
            next_nodes[node][1] = -1;
        }

        assert(next_nodes[node][0] != next_nodes[node][1]);
    }
}


// find the previous possible branches in a removal path
void get_prev_removal_nodes(const LocalTree *tree1, const LocalTree *tree2,
                            const Spr &spr2, const int *mapping2,
                            int node, int prev_nodes[2])
{
    const int nnodes = tree1->nnodes;

    // make inverse mapping
    int inv_mapping[nnodes];
    fill(inv_mapping, inv_mapping + nnodes, -1);

    for (int i=0; i<nnodes; i++)
        if (mapping2[i] != -1)
            inv_mapping[mapping2[i]] = i;

    // get first transition
    prev_nodes[0] = inv_mapping[node];
    if (prev_nodes[0] == -1) {
        // there is no inv_mapping because node is recoal
        // the node spr.coal_node therefore is the previous node
        prev_nodes[0] = spr2.coal_node;

        // get optional second transition
        int sib = tree1->get_sibling(spr2.recomb_node);
        if (sib == spr2.coal_node) {
            prev_nodes[1] = tree1->nodes[sib].parent;
        } else
            prev_nodes[1] = -1;
    } else {
        // get optional second transition
        int sib = tree1->get_sibling(spr2.recomb_node);
        if (mapping2[sib] == node && sib != spr2.coal_node) {
            prev_nodes[1] = tree1->nodes[sib].parent;
        } else
            prev_nodes[1] = -1;
    }
}


// find the previous possible branches in a removal path
void get_all_prev_removal_nodes(const LocalTree *tree1, const LocalTree *tree2,
                                const Spr &spr2, const int *mapping2,
                                int prev_nodes[][2])
{
    const int nnodes = tree1->nnodes;

    // make inverse mapping
    int inv_mapping[nnodes];
    fill(inv_mapping, inv_mapping + nnodes, -1);
    for (int i=0; i<nnodes; i++)
        if (mapping2[i] != -1)
            inv_mapping[mapping2[i]] = i;

    for (int node=0; node<tree1->nnodes; node++) {
        // get first transition
        prev_nodes[node][0] = inv_mapping[node];
        if (prev_nodes[node][0] == -1) {
            // there is no inv_mapping because node is recoal
            // the node spr.coal_node therefore is the previous node
            prev_nodes[node][0] = spr2.coal_node;
            
            // get optional second transition
            int sib = tree1->get_sibling(spr2.recomb_node);
            if (sib == spr2.coal_node) {
                prev_nodes[node][1] = tree1->nodes[sib].parent;
            } else
                prev_nodes[node][1] = -1;
        } else {
            // get optional second transition
            int sib = tree1->get_sibling(spr2.recomb_node);
            if (mapping2[sib] == node && sib != spr2.coal_node) {
                prev_nodes[node][1] = tree1->nodes[sib].parent;
            } else
                prev_nodes[node][1] = -1;
        }
        
        assert(prev_nodes[node][0] != prev_nodes[node][1]);
    }
}



// sample a removal path forward along an ARG
void sample_arg_removal_path_forward(LocalTrees *trees, LocalTrees::iterator it,
                                     int node, int *path, int i)
{
    path[i++] = node;
    LocalTree *last_tree = NULL;

    for (; it != trees->end(); ++it) {
        LocalTree *tree = it->tree;

        if (last_tree) {
            int next_nodes[2];
            Spr *spr = &it->spr;
            int *mapping = it->mapping;
            get_next_removal_nodes(last_tree, tree, *spr, mapping,
                                   path[i-1], next_nodes);
            int j = (next_nodes[1] != -1 ? irand(2) : 0);
            path[i++] = next_nodes[j];
            
            // ensure that a removal path re-enters the local tree correctly
            if (last_tree->root == path[i-2] && tree->root != path[i-1]) {
                assert(spr->coal_node == last_tree->root);
            }
        }
        
        last_tree = tree;
    }
}


// sample a removal path backward along an ARG
void sample_arg_removal_path_backward(
    LocalTrees *trees, LocalTrees::iterator it, int node, int *path, int i)
{
    path[i--] = node;
    
    LocalTree *tree2 = it->tree;
    Spr *spr2 = &it->spr;
    int *mapping2 = it->mapping;
    --it;

    for (; it != trees->end(); --it) {
        int prev_nodes[2];
        LocalTree *tree1 = it->tree;
        assert(!spr2->is_null());

        get_prev_removal_nodes(tree1, tree2, *spr2, mapping2, 
                               path[i+1], prev_nodes);        
        int j = (prev_nodes[1] != -1 ? irand(2) : 0);
        path[i--] = prev_nodes[j];

        spr2 = &it->spr;
        mapping2 = it->mapping;
        tree2 = tree1;
    }
}


// sample a removal path that goes through a particular node and position
// in the ARG
void sample_arg_removal_path(LocalTrees *trees, int node, int pos, int *path)
{
    // search for block with pos
    LocalTrees::iterator it = trees->begin();
    int end = trees->start_coord;
    int i = 0;
    for (; it != trees->end(); ++it, i++) {
        int start = end;
        end += it->blocklen;
        if (start <= pos && pos < end)
            break;
    }
    
    // search forward
    sample_arg_removal_path_forward(trees, it, node, path, i);
    sample_arg_removal_path_backward(trees, it, node, path, i);
}


// sample a removal path that starts at a particular node in the ARG
void sample_arg_removal_path(LocalTrees *trees, int node, int *path)
{
    // search for block with pos
    LocalTrees::iterator it = trees->begin();
    sample_arg_removal_path_forward(trees, it, node, path, 0);
}


// sample a removal path that only contains leaves
// TODO: this could be simplified, calculating leaf path are easy
void sample_arg_removal_leaf_path(LocalTrees *trees, int node, int *path)
{
    int i = 0;
    path[i++] = node;
    LocalTree *last_tree = NULL;

    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it) {
        LocalTree *tree = it->tree;

        if (last_tree) {
            int next_nodes[2];
            Spr *spr = &it->spr;
            int *mapping = it->mapping;
            get_next_removal_nodes(last_tree, tree, *spr, mapping,
                                   path[i-1], next_nodes);
            path[i++] = next_nodes[0];
            
            // ensure that a removal path re-enters the local tree correctly
            if (last_tree->root == path[i-2] && tree->root != path[i-1]) {
                assert(spr->coal_node == last_tree->root);
            }
        }
        
        last_tree = tree;
    }
}



// sample a removal path that perfers recombination baring branches
void sample_arg_removal_path_recomb(LocalTrees *trees, double recomb_preference,
                                    int *path)
{
    const int ntrees = trees->get_num_trees();
    const int nnodes = trees->nnodes;
    
    // build forward table for removal path sampling
    typedef int next_row[2];
    double **forward = new_matrix<double>(ntrees, nnodes);
    next_row **backptrs = new_matrix<next_row>(ntrees, nnodes);
    double **trans = new_matrix<double>(ntrees, nnodes);


    // calculate prior
    fill(forward[0], forward[0] + nnodes, 1.0 / nnodes);

    // compute forward table
    LocalTrees::iterator it=trees->begin();
    LocalTree *last_tree = it->tree;
    int next_nodes[nnodes][2];
    ++it;
    for (int i=1; i<ntrees; i++, ++it) {
        LocalTree *tree = it->tree;
        int *mapping = it->mapping;
        get_all_next_removal_nodes(last_tree, tree, it->spr, mapping,
                                   next_nodes);
        get_all_prev_removal_nodes(last_tree, tree, it->spr, mapping,
                                   backptrs[i]);

        next_row *prev_nodes = backptrs[i];
        for (int j=0; j<nnodes; j++) {
            int k = next_nodes[j][0];
            assert(prev_nodes[k][0] == j || prev_nodes[k][1] == j);
            k = next_nodes[j][1];
            if (k != -1)
                assert(prev_nodes[k][0] == j || prev_nodes[k][1] == j);
        }

        // get next spr
        LocalTrees::iterator it2 = it;
        it2++;
        Spr &spr2 = it->spr;
        
        // calc transition probs
        for (int j=0; j<nnodes; j++)
            trans[i][j] = (next_nodes[j][1] != -1 ? .5 : 1.0);

        // calc forward column
        double norm = 0.0;
        for (int j=0; j<nnodes; j++) {
            double sum = 0.0;
            for (int ki=0; ki<2; ki++) {
                int k = backptrs[i][j][ki];
                if (k == -1)
                    continue;
                sum += trans[i][j] * forward[i-1][k];
            }
            
            double emit = ((!spr2.is_null() && spr2.recomb_node == j) ?
                           recomb_preference : 1.0 - recomb_preference);
            forward[i][j] = sum * emit;

            //printf("forward[%d][%d] = %f, %f, %f (%f)\n", i, j, forward[i][j],
            //       emit, sum, recomb_preference);
            assert(!isnan(forward[i][j]));
            
            norm += forward[i][j];
        }

        // normalize column for numerical stability
        for (int j=0; j<nnodes; j++)
            forward[i][j] /= norm;
        

        last_tree = tree;
    }


    // choose last branch
    int i = ntrees-1;
    path[i] = sample(forward[i], nnodes);
    //for (int j=0; j<nnodes; j++)
    //    printf("forward[%d][%d] = %f\n", i, j, forward[i][j]);

    // stochastic traceback
    int j = path[i];
    i--;
    for (; i>=0; i--) {
        //printf("backptr[%d][%d] = {%d, %d}\n",
        //       i+1, j, backptrs[i+1][j][0], backptrs[i+1][j][1]);
        if (backptrs[i+1][j][1] == -1) {
            // only one path
            j = path[i] = backptrs[i+1][j][0];
        } else {
            // fork, sample path
            double probs[2];
            probs[0] = forward[i][backptrs[i+1][j][0]] * trans[i][j];
            probs[1] = forward[i][backptrs[i+1][j][1]] * trans[i][j];
            int ji = sample(probs, 2);
            j = path[i] = backptrs[i+1][j][ji];
        }
    }
    
    
    // clean up
    delete_matrix<double>(forward, ntrees);
    delete_matrix<next_row>(backptrs, ntrees);
    delete_matrix<double>(trans, ntrees);

    /*
    // DEBUG
    int nrecomb = 0;
    it = trees->begin();
    for (int i=0; i<ntrees; i++, ++it) {
        if (path[i] == it->spr.recomb_node)
            nrecomb++;
    }
    printf("nrecomb resampled = %d\n", nrecomb);
    */

}



//=============================================================================
// internal branch adding and removing


// update an SPR and mapping after adding a new internal branch
void add_spr_branch(LocalTree *tree, LocalTree *last_tree, 
                    State state, State last_state,
                    Spr *spr, int *mapping,
                    int subtree_root, int last_subtree_root)
{
    // get tree info
    LocalNode *nodes = tree->nodes;
    LocalNode *last_nodes = last_tree->nodes;
    int node2 = state.node = state.node;
    int last_newcoal = last_nodes[last_subtree_root].parent;

    // determine newcoal
    int newcoal;
    if (state.node != -1) {
        newcoal = nodes[subtree_root].parent;
    } else {
        // fully specified tree
        if (mapping[last_subtree_root] != -1)
            newcoal = nodes[mapping[last_subtree_root]].parent;
        else {
            int sib = last_tree->get_sibling(spr->recomb_node);
            assert(mapping[sib] != -1);
            newcoal = nodes[mapping[sib]].parent;
        }
    }
                
    // set default new node mapping 
    mapping[last_newcoal] = newcoal;
        
    // parent of recomb node should be the recoal point
    // however, if it equals newcoal, then either (1) the recomb branch is 
    // renamed, (2) there is mediation, or (3) new branch escapes
    int recoal = nodes[mapping[spr->recomb_node]].parent;
    if (recoal == newcoal) {
        if (mapping[last_state.node] == node2) {
            // (1) recomb is above coal state, we rename spr recomb node
            spr->recomb_node = last_newcoal;
        } else {
            // if this is a mediated coal, then state should equal recomb
            int state_node = state.node;
            if (spr->coal_time == last_nodes[last_newcoal].age &&
                state_node == mapping[spr->recomb_node]) {
                // (3) this is a mediated coal, rename coal node and time
                if (state.time < last_nodes[last_subtree_root].age) {
                    spr->coal_node = last_tree->get_sibling(spr->recomb_node);
                    spr->coal_time = state.time;
                } else{
                    spr->coal_node = last_subtree_root;
                    spr->coal_time = state.time;
                }
                assert(spr->coal_time >= last_nodes[spr->coal_node].age);
            } else {
                // (2) this is the new branch escaping
                // no other updates are necessary
            }
        }
    } else {
        // the other possibility is that newcoal is under recoal point
        // if newcoal is child of recoal, then coal is renamed
        int *c = nodes[recoal].child;
        if (c[0] == newcoal || c[1] == newcoal) {
            // we either coal above the newcoal or our existing
            // node just broke and newcoal was underneath.
                
            // if newcoal was previously above spr->coal_node
            // then we rename the spr coal node
            if (last_nodes[spr->coal_node].parent == last_newcoal)
                spr->coal_node = last_newcoal;
            assert(spr->coal_time >= last_nodes[spr->coal_node].age);
            int p = last_nodes[spr->coal_node].parent;
            if (p != -1)
                assert(spr->coal_time <= last_nodes[p].age);
        }
    }
            
    // determine if mapping of new node needs to be changed
    // newcoal was parent of recomb, it is broken
    if (last_nodes[spr->recomb_node].parent == last_newcoal) {
        mapping[last_newcoal] = -1;
        int p = last_nodes[last_newcoal].parent;
        if (p != -1)
            mapping[p] = newcoal;
    } else {
        // newcoal was not broken
        // find child without recomb or coal on it
        int x = last_newcoal;
        int y = last_nodes[x].child[0];
        if (y == spr->coal_node)
            y = last_nodes[x].child[1];
        if (mapping[y] == -1)
            y = last_tree->get_sibling(spr->recomb_node);
        if (y == spr->coal_node)
            y = last_nodes[x].child[1];
        mapping[last_newcoal] = nodes[mapping[y]].parent;
    }


    // DEBUG
    if (last_tree->nodes[spr->recomb_node].parent == -1) {
        printf("newcoal = %d, last_newcoal = %d, recoal = %d\n",
               newcoal, last_newcoal, recoal);
        assert(false);
    }

    assert(assert_spr(last_tree, tree, spr, mapping));
}



// Add a branch to a partial ARG
void add_arg_thread_path(LocalTrees *trees, int ntimes, int *thread_path, 
                         vector<int> &recomb_pos, vector<NodePoint> &recombs)
{
    States states;
    LocalTree *last_tree = NULL;
    State last_state;
    int last_subtree_root = -1;
    unsigned int irecomb = 0;
    int end = trees->start_coord;
    
    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it) 
    {
        LocalTree *tree = it->tree;
        LocalNode *nodes = tree->nodes;
        Spr *spr = &(it->spr);
        State state;
        int start = end;
        end += it->blocklen;
        const int subtree_root = nodes[tree->root].child[0];
        get_coal_states_internal(tree, ntimes, states);
        int nstates = states.size();

        
        // detect whether local tree is partial
        if (nodes[tree->root].age > ntimes) {
            assert(nstates > 0);
            // modify local tree according to thread path

            state = states[thread_path[start]];
            Spr add_spr(subtree_root, nodes[subtree_root].age,
                        state.node, state.time);
            apply_spr(tree, add_spr);
        } else {
            // set null state
            state.node = -1;
            state.time = -1;
        }

        // fix spr
        // update mapping and spr
        int *mapping = it->mapping;
        if (mapping) {
            if (last_state.node != -1) {
                add_spr_branch(tree, last_tree, state, last_state,
                               spr, mapping, subtree_root, last_subtree_root);
            }
        }

        
        // break this block for each new recomb within this block
        for (;irecomb < recombs.size() && 
              recomb_pos[irecomb] < end; irecomb++) {
            
            int pos = recomb_pos[irecomb];
            LocalNode *nodes = tree->nodes;

            assert(nstates > 0);

            state = states[thread_path[pos]];
            last_state = states[thread_path[pos-1]];
            int newcoal = nodes[subtree_root].parent;
            
            // assert that thread time is still on track
            assert(tree->nodes[newcoal].age == last_state.time);
            
            // determine real name of recomb node
            // it may be different due to adding a new branch
            Spr spr2;
            spr2.recomb_node = recombs[irecomb].node;
            spr2.recomb_time = recombs[irecomb].time;
            assert(spr2.recomb_time <= tree->nodes[newcoal].age);

            // determine coal node and time
            if (spr2.recomb_node == subtree_root) {
                // recomb on new branch, coal given thread
                spr2.coal_node = state.node;
                
                // rename coal node due to newcoal underneath
                if (state.node == last_state.node &&
                    state.time > last_state.time)
                    spr2.coal_node = newcoal;
            } else {
                // recomb in maintree, coal on new branch
                if (state.time > last_state.time)
                    spr2.coal_node = nodes[subtree_root].parent;
                else
                    spr2.coal_node = subtree_root;
            }
            spr2.coal_time = state.time;

            
            // determine mapping:
            // all nodes keep their name expect the broken node, which is the
            // parent of recomb
            int *mapping2 = new int [tree->capacity];
            for (int j=0; j<tree->nnodes; j++)
                mapping2[j] = j;
            mapping2[nodes[spr2.recomb_node].parent] = -1;

            
            // make new local tree and apply SPR operation
            LocalTree *new_tree = new LocalTree(tree->nnodes, tree->capacity);
            new_tree->copy(*tree);
            apply_spr(new_tree, spr2);

            // calculate block end
            int block_end;
            if (irecomb < recombs.size() - 1)
                // use next recomb in this block to determine block end
                block_end = min(recomb_pos[irecomb+1], end);
            else
                // no more recombs in this block
                block_end = end;
           
            // insert new tree and spr into local trees list
            it->blocklen = pos - start;
            ++it;
            it = trees->trees.insert(it, 
                LocalTreeSpr(new_tree, spr2, block_end - pos, mapping2));
            

            // assert tree and SPR
            assert(assert_tree(new_tree));
            assert(new_tree->nodes[newcoal].age == state.time);
            assert(assert_spr(tree, new_tree, &spr2, mapping2));

            // remember the previous tree for next iteration of loop
            tree = new_tree;
            nodes = tree->nodes;
            start = pos;
        }
        
        // record previous local tree information
        last_tree = tree;
        last_state = state;
        last_subtree_root = subtree_root;
    }

    assert_trees(trees);
}



// get the new names for nodes due to collapsing null SPRs
LocalTree *get_actual_nodes(LocalTrees *trees, LocalTrees::iterator it, 
                            int *nodes)
{
    LocalTree *tree = it->tree;
    const int nnodes = tree->nnodes;

    // get current nodes
    for (int i=0; i<nnodes; i++)
        nodes[i] = i;
    
    LocalTrees::iterator it2 = it;
    ++it2;
    for (;it2 != trees->end() && it2->spr.is_null(); ++it2) {
        // compute transitive mapping
        int *mapping = it2->mapping;
        for (int i=0; i<nnodes; i++) {
            if (mapping[i] != -1)
                nodes[i] = mapping[nodes[i]];
            else
                nodes[i] = -1;
        }
    }
    
    --it2;
    return it2->tree;
}


// Removes a thread path from an ARG and returns a partial ARG
void remove_arg_thread_path(LocalTrees *trees, const int *removal_path, 
                            int maxtime, int *original_thread)
{
    LocalTree *tree = NULL;
    State *original_states = NULL;

    // prepare original thread array if requested
    if (original_thread) {
        original_states = new State [trees->length()];
    }

    
    int i=0;
    int end = trees->start_coord;
    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it, i++) 
    {
        LocalTree *last_tree = tree;
        tree = it->tree;
        LocalNode *nodes = tree->nodes;
        int start = end;
        end += it->blocklen;

        int removal_node = removal_path[i];

        if (removal_node == tree->root) {
            // fix previous mapping
            if (it->mapping && removal_path[i-1] != last_tree->root)
                it->mapping[last_tree->root] = -1;

            // record thread
            if (original_states) {
                for (int j=start; j<end; j++) {
                    original_states[j].set(-1, -1) ;
                }
            }

            // removal path has "fallen off the top" there is nothing to edit
            continue;
        }
        
        // modify local into subtree-maintree format
        int broken_node = nodes[removal_node].parent;
        int coal_time = nodes[broken_node].age;
        int broken_child = tree->get_sibling(removal_node);
        Spr removal_spr(removal_node, nodes[removal_node].age,
                        tree->root, maxtime);

        apply_spr(tree, removal_spr);
            
        // determine subtree and maintree roots
        int subtree_root = removal_node;
        int maintree_root = tree->get_sibling(subtree_root);
        
        // ensure subtree is the first child of the root
        int *c = nodes[tree->root].child;
        if (c[0] == maintree_root) {
            c[0] = subtree_root;
            c[1] = maintree_root;
        }
        
        // fix previous mapping
        if (it->mapping) {
            assert(last_tree);
            if (removal_path[i-1] != last_tree->root)
                it->mapping[last_tree->root] = tree->root;
        }

        // record thread
        if (original_states) {
            for (int j=start; j<end; j++)
                original_states[j].set(broken_child, coal_time);
        }


        // get next tree
        LocalTrees::iterator it2 = it;
        ++it2;
        if (it2 == trees->end())
            continue;


        // fix SPR
        Spr *spr = &it2->spr;
        int *mapping = it2->mapping;

        // if recomb is on branch removed, prune it
        if (spr->recomb_node == removal_node) {
            int p = nodes[spr->recomb_node].parent;
            assert(mapping[p] != -1 || p == tree->root);
            spr->set_null();
            continue;
        }

        // see if recomb node is renamed
        if (spr->recomb_node == broken_node) {
            spr->recomb_node = broken_child;
        }
        
        // detect branch path splits
        int next_nodes[2];
        get_next_removal_nodes(tree, it2->tree, *spr, mapping, 
                               removal_path[i], next_nodes);

        if (spr->coal_node == removal_node) {
            if (removal_path[i+1] == next_nodes[0]) {
                // removal path chooses lower path
                // note: sister_node = broken_child
                
                if (spr->recomb_node == broken_child) {
                    // spr is now bubble, prune it
                    int p = nodes[spr->recomb_node].parent;
                    assert(mapping[p] != -1 || p == tree->root);
                    spr->set_null();
                    continue;
                } else {
                    // recomb is on non-sister branch, therefore it is a
                    // mediated coalescence
                    spr->coal_node = broken_child;
                    spr->coal_time = coal_time;
                }
            } else {
                // removal path chooses upper path
                // keep spr recoal where it is

                // assert that upper path is the new recoal node
                // nobody should map to the new recoal node
                for (int j=0; j<tree->nnodes; j++)
                    assert(mapping[j] != removal_path[i+1]);
            }
        } else if (spr->coal_node == broken_node) {
            // rename spr recoal
            spr->coal_node = broken_child;
        }
        
        // check for bubbles
        if (spr->recomb_node == spr->coal_node) {
            int p = nodes[spr->recomb_node].parent;
            assert(mapping[p] != -1 || p == tree->root);
            spr->set_null();
            continue;
        }

        // ensure broken node maps to -1
        int spr_broken_node = nodes[spr->recomb_node].parent;
        mapping[spr_broken_node] = -1;
        
        // assert spr
        if (last_tree && !it->spr.is_null())
            assert_spr(last_tree, tree, &it->spr, it->mapping);
    }

    // record original thread
    if (original_states) {
        // TODO: may not want to assume ntimes = maxtime - 1
        const int ntimes = maxtime - 1;
        const int nnodes = trees->nnodes;
        States states;

        int end = trees->start_coord;
        for (LocalTrees::iterator it=trees->begin(); it!=trees->end(); ++it) {
            int start = end;
            end += it->blocklen;
            
            int nodes_lookup[nnodes];
            LocalTree *tree2 = get_actual_nodes(trees, it, nodes_lookup);

            get_coal_states_internal(tree2, ntimes, states);
            int nstates = states.size();
            NodeStateLookup lookup(states, nnodes);

            for (int i=start; i<end; i++) {
                if (nstates == 0) {
                    original_thread[i] = 0;
                } else {
                    int statei = lookup.lookup(
                        nodes_lookup[original_states[i].node],
                        original_states[i].time);
                    assert(statei != -1);
                    original_thread[i] = statei;
                }
            }
        }
        
        delete [] original_states;
    }
    
    // remove extra trees
    remove_null_sprs(trees);
    
    assert_trees(trees);
}



//=============================================================================
// C interface

extern "C" {

void arghmm_sample_arg_removal_path(LocalTrees *trees, int node, int *path)
{
    sample_arg_removal_path(trees, node, path);
}

void arghmm_sample_arg_removal_path2(LocalTrees *trees, int node, 
                                     int pos, int *path)
{
    sample_arg_removal_path(trees, node, pos, path);
}

void arghmm_sample_arg_removal_leaf_path(LocalTrees *trees, int node, 
                                           int *path)
{
    sample_arg_removal_leaf_path(trees, node, path);
}

void arghmm_sample_arg_removal_path_recomb(LocalTrees *trees, 
                                           double recomb_preference, int *path)
{
    sample_arg_removal_path_recomb(trees, recomb_preference, path);
}


void arghmm_remove_arg_thread_path(LocalTrees *trees, int *removal_path, 
                                   int maxtime)
{
    remove_arg_thread_path(trees, removal_path, maxtime);
}

void arghmm_remove_arg_thread_path2(LocalTrees *trees, int *removal_path, 
                                    int maxtime, int *original_thread)
{
    remove_arg_thread_path(trees, removal_path, maxtime, original_thread);
}


void arghmm_get_thread_times(LocalTrees *trees, int ntimes, int *path, 
                             int *path_times)
{
    States states;

    int end = trees->start_coord;
    for (LocalTrees::iterator it=trees->begin(); it != trees->end(); ++it) {
        int start = end;
        end += it->blocklen;
        LocalTree *tree = it->tree;
        get_coal_states_internal(tree, ntimes, states);

        for (int i=start; i<end; i++)
            path_times[i] = states[path[i]].time;
    }
}



} // extern C

} // namespace arghmm
