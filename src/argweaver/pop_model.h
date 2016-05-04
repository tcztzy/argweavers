//=============================================================================
// ArgHmm multiple population model


#ifndef ARGWEAVER_POP_MODEL_H
#define ARGWEAVER_POP_MODEL_H

// c/c++ includes
#include <math.h>
#include <set>
#include <map>
#include <list>
#include <algorithm>
#include <vector>
#include <string>

// arghmm includes
#include "common.h"
#include "model.h"

namespace argweaver {

class ArgModel;
class LocalNode;

class PopulationPath {
 public:
    PopulationPath(int ntime) {
        pop = vector<int>(ntime, 0);
        prob = 1.0;
    }
    PopulationPath(const PopulationPath &other) {
        prob = other.prob;
        pop = vector<int>(other.pop);
    }
    bool operator< (const PopulationPath &other) const {
        if (pop.size() != other.pop.size())
            return ( pop.size() < other.pop.size());
        if (prob != other.prob)
            return (prob < other.prob);
        for (unsigned int i=0; i < pop.size(); i++)
            if (pop[i] != other.pop[i]) return (pop[i] < other.pop[i]);
        return false;
    }
    void set(int time, int t_pop, double currprob=1.0) {
        pop[time] = t_pop;
        prob *= currprob;
    }
    int get(int time) const {
        return pop[time];
    }
    void print() const;

    double prob;
    vector<int> pop;
};  /* class PopulationPath */


/* Represents the migration matrix for a single time period.
   Is simply a two-dimensional matrix giving the probability of
   migrating from each pop to each other pop.
 */
class MigMatrix {
 public:
   MigMatrix() {
       npop=0;
   }
   MigMatrix(int npop) :
    npop(npop) {
        init();
   }
    MigMatrix(const MigMatrix &other) {
        npop = other.npop;
        init();
        if (npop > 0)
            std::copy(other.mat, other.mat + npop*npop, mat);
    }
    ~MigMatrix() {
        if (npop > 0) delete [] mat;
    }
    /*    void copy(MigMatrix &other) {
        std::copy(other.mat, other.mat + pop*pop, mat);
        }*/
    void set(int from_pop, int to_pop, double val) {
        mat[from_pop * npop + to_pop] = val;
    }
    void update(int from_pop, int to_pop, double val) {
        set(from_pop, to_pop, val);
        set(from_pop, from_pop, 1.0-val);
    }
    void resize(int new_npop) {
        if (new_npop != npop) {
            if (npop > 0) delete [] mat;
            npop = new_npop;
            init();
        }
    }
    double get(int from_pop, int to_pop) const {
        return mat[from_pop * npop + to_pop];
    }

    int npop;
    double *mat;

 protected:
    void init() {
        if (npop > 0) {
            mat = new double[npop*npop]();
            for (int i=0; i < npop; i++)
                set(i, i, 1.0);
        }
    }
};


class PathProb {
 public:
    PathProb(int path_idx, double prob) : prob(prob) {
        path.clear();
        path.insert(path_idx);
    };
   void update_prob(const vector <PopulationPath> &all_paths);
   void print() const;

   int first_path() const {
       if (path.size() == 0) return -1;
       return *path.begin();
   }
   void add_path(int p, double pr) {
       path.insert(p);
       prob += prob;
   }

   set<int> path;
   double prob;
};


class SubPath {
 public:
    SubPath() {
        path_map = NULL;
        unique_subs.clear();
    }
    void set_size(unsigned int max_paths) {
        path_map = new int[max_paths];
        for (unsigned int i=0; i < max_paths; i++)
            path_map[i] = -1;
    }
    void add_path_to_subpath(int path, int idx, int prob) {
        assert((int)unique_subs.size() > idx);
        unique_subs[idx].add_path(path, prob);
        path_map[path] = idx;
    }
    void new_subpath(int path, int prob) {
        PathProb p(path, prob);
        path_map[path] = unique_subs.size();
        unique_subs.push_back(p);
    }
    unsigned int size() const {
        return unique_subs.size();
    }
    double prob(int i) const {
        assert(i <(int)unique_subs.size());
        return unique_subs[i].prob;
    }
    int first_path(int idx) const {
        assert(idx < (int)unique_subs.size());
        return unique_subs[idx].first_path();
    }
    void print() {
        for (unsigned int i=0; i < unique_subs.size(); i++) {
            unique_subs[i].print();
        }
    }
    void update_probs(const vector<PopulationPath> &all_paths) {
        for (unsigned int i=0; i < unique_subs.size(); i++) {
            unique_subs[i].update_prob(all_paths);
        }
    }
    vector<PathProb> unique_subs;
    int *path_map;
};


/* PopulationTree
   is implemented as a vector of migration matrices. One element for each
   half time interval.
 */
class PopulationTree {

 public:
  PopulationTree(int npop, const ArgModel *model);
  PopulationTree(const PopulationTree &other);
  ~PopulationTree();
  void update_npop(int new_npop);
  void add_migration(int t, int from_pop, int to_pop, double prob);
  void set_up_population_paths();
  void update_population_probs();
  bool paths_equal(int path1, int path2, int t1, int t2) const;
  void print_all_paths() const;
  void print_sub_path(vector<PathProb> &subpath) const;
  void print_sub_paths() const;
  void print() const;
  int num_pop_paths() const {return all_paths.size();}
  int path_pop(int path, int t) const {
      return all_paths[path].get(t);
  }
  bool num_paths(int t1, int p1, int t2, int p2) const {
      return sub_paths[t1][t2][p1][p2].size();
  }
  /* This one returns the probability of the i^{th} unique path that
     starts in pop p1 at time t1 and goes to pop p2 at time t2
   */
  double subpath_prob(int t1, int p1, int t2, int p2, unsigned int i) const {
      return sub_paths[t1][t2][p1][p2].prob(i);
  }
  /* This one returns the sum of the probability of all paths which are
     consistent with the given path from time t1 to time t2. The given path
     should have an index between 0 and all_paths.size()
   */
  double path_prob(int path, int t1, int t2) const {
      assert(path >=0 && path < (int)all_paths.size());
      int pop1 = all_paths[path].get(t1);
      int pop2 = all_paths[path].get(t2);
      int idx = sub_paths[t1][t2][pop1][pop2].path_map[path];
      if (idx < 0) {
          // NOTE: could return 0 here but should check why we ever
          // get here in that case
          assert(0);
      }
      return sub_paths[t1][t2][pop1][pop2].prob(idx);
  }
  int unique_path(int t1, int p1, int t2, int p2, unsigned int i) const {
      return sub_paths[t1][t2][p1][p2].first_path(i);
  }
  bool path_possible(int t1, int p1, int t2, int p2) {
      for (unsigned int i=0; i < sub_paths[t1][t2][p1][p2].size(); i++) {
          if (sub_paths[t1][t2][p1][p2].prob(i) > 0) return true;
      }
      return false;
  }
  int most_likely_path(int start_pop) const {
      int best=-1;
      double best_prob=0;
      for (unsigned int i=0; i < all_paths.size(); i++) {
          if (all_paths[i].get(0) != start_pop) continue;
          if (best == -1 || all_paths[i].prob > best_prob) {
              best_prob = all_paths[i].prob;
              best=i;
          }
      }
      assert(best >= 0);
      return best;
  }

  inline set<int>* get_equivalent_paths(int p, int t1, int t2) const {
      assert(t1 <= t2);
      int p1 = all_paths[p].get(t1);
      int p2 = all_paths[p].get(t2);
      int idx = sub_paths[t1][t2][p1][p2].path_map[p];
      assert(idx >= 0);
      return &(sub_paths[t1][t2][p1][p2].unique_subs[idx].path);
  }

  int get_pop(int path, int time) const {
      return all_paths[path].get(time);
  }

  int final_pop() const;

  // Returns a path consistent with path1 from t1 to t2 (inclusive),
  // and from t2 to t3 (inclusive).
  // requires t1 <= t2 <= t3
  // Note that there is not always a unique answer;
  // the first one found is returned
  // if require_exists is true, exit with error if no consistent path found
  // if require_exists is false, return -1 when none found
  int consistent_path(int path1, int path2, int t1, int t2, int t3,
                      bool require_exists=true) const;

  int path_to_root(const LocalNode *nodes, int node) const;

  //  npop is the total number of non-ancestral populations
  int npop;
  const ArgModel *model;

  vector<MigMatrix> mig_matrix;

  // this is the set of distinct paths from t=0 to t=ntimes-1
  vector<PopulationPath> all_paths;

  //sub_paths[t1][t2][p1][p2] is a vector of distinct paths from
  // time t1, pop p1 to time t2, pop p2.
  // If multiple paths from all_paths are identical in this interval,
  // they are combined in the same PathProb element and their
  // probabilities are summed.
  SubPath ****sub_paths;

  // max_matching_path[p1][p2][t] is -1 if the path p1 and p2 have different
  // populations at time t. Otherwise it is the maximum t1 such that
  // paths p1 and p2 match from time t to t1. It is set in
  // set_up_population_paths
  int ***max_matching_path;



 private:
    void getAllPopulationPathsRec(PopulationPath &curpath,
                                  int cur_time, int end_time, int cur_pop);
    int find_sub_path(int path, const SubPath &subpath,
                      int t1, int t2);

};  /* class PopulationTree */

void read_population_tree(FILE *infile, PopulationTree *pop_tree);

int get_closest_half_time(double treal, const double *time_steps, int ntime);


} // namespace argweaver


#endif // ARGWEAVER_POP_MODEL_H