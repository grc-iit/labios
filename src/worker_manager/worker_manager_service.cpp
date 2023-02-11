/*
 * Copyright (C) 2019  SCS Lab <scs-help@cs.iit.edu>, Hariharan
 * Devarajan <hdevarajan@hawk.iit.edu>, Anthony Kougkas
 * <akougkas@iit.edu>, Xian-He Sun <sun@iit.edu>
 *
 * This file is part of Labios
 *
 * Labios is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
//
// Created by hdevarajan on 5/14/18.
//

#include "worker_manager_service.h"
#include <labios/common/constants.h>
#include <zconf.h>

#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

enum Distribution { UNIFORM, LINEAR, EXPONENTIAL };

// Constants
int MAX_SCORE = 100;
int NUM_WORKERS = 100;               // Total number of workers
int NUM_BUCKETS = 2;                 // % of buckets to use for sorting
Distribution distribution = UNIFORM; // Way to distribute workers to buckets

std::vector<int>
    whichBucket;         // Keeps track of which bucket index the worker is in
std::vector<int> scores; // Keeps track of worker score

struct ScoreUpdate {
  int workerID;
  int score;
};

class WorkerCompare {
public:
  bool operator()(const int &lhs, const int &rhs) const {
    return scores[lhs] < scores[rhs];
  }
};

class Bucket {
  std::set<int> workers;

public:
  int getWorkerId(int idx) { return *(std::next(workers.begin(), idx)); }

  bool isPromotionCandidate(int score) {
    return score > scores[*workers.begin()];
  }

  bool isDemotionCandidate(int score) {
    return score < scores[*workers.rbegin()];
  }

  void updateWorker(int workerID, int workerScore) {
    removeWorker(workerID);
    scores[workerID] = workerScore;
    insertWorker(workerID);
  }

  void removeWorker(int worker) { workers.erase(worker); }

  void insertWorker(int worker) { workers.insert(worker); }

  void promote(int workerID, int newScore, Bucket &otherBucket) {
    int swap = *workers.begin();
    removeWorker(swap);
    insertWorker(workerID);
    otherBucket.removeWorker(workerID);
    otherBucket.insertWorker(swap);

    // Update bucket addresses
    int bucketSwap = whichBucket[swap];
    whichBucket[swap] = whichBucket[workerID];
    whichBucket[workerID] = bucketSwap;
  }

  void demote(int workerID, int newScore, Bucket &otherBucket) {
    int swap = *workers.rbegin();
    removeWorker(swap);
    insertWorker(workerID);
    otherBucket.removeWorker(workerID);
    otherBucket.insertWorker(swap);

    // Update bucket addresses
    int bucketSwap = whichBucket[swap];
    whichBucket[swap] = whichBucket[workerID];
    whichBucket[workerID] = bucketSwap;
  }

  int getSize() { return workers.size(); }
};

void populate_buckets_uniform(std::vector<Bucket> &);
void populate_buckets_linear(std::vector<Bucket> &);
void populate_buckets_exponential(std::vector<Bucket> &);

std::vector<Bucket> buckets;

// Call this function when there is a new update
void updateScore(int workerID, int newScore) {
  bool foundHome = false;
  while (!foundHome) {
    int bucketIndex = whichBucket[workerID];
    Bucket &bucket = buckets[bucketIndex];
    if (bucketIndex > 0 &&
        buckets[bucketIndex - 1].isPromotionCandidate(newScore)) {
      buckets[bucketIndex - 1].promote(workerID, newScore, bucket);
      whichBucket[workerID] = bucketIndex - 1;
    } else if (bucketIndex < NUM_BUCKETS - 1 &&
               buckets[bucketIndex + 1].isDemotionCandidate(newScore)) {
      buckets[bucketIndex + 1].demote(workerID, newScore, bucket);
      whichBucket[workerID] = bucketIndex + 1;
    } else {
      bucket.updateWorker(workerID, newScore);
      foundHome = true;
    }
  }
}

void populate_buckets_uniform(std::vector<Bucket> &buckets) {
  for (int i = 0; i < NUM_WORKERS; i++) {
    whichBucket[i] = i % NUM_BUCKETS;
    buckets[i % NUM_BUCKETS].insertWorker(i);
  }
}

void populate_buckets_linear(std::vector<Bucket> &buckets) {
  // Determine slope
  int step = NUM_WORKERS / ((NUM_BUCKETS * (NUM_BUCKETS + 1)) / 2);

  int workerID = 0;
  for (int i = 0; i < NUM_BUCKETS; i++) {
    int size = (i + 1) * step;
    for (int j = 0; j < size; j++) {
      whichBucket[workerID] = i;
      buckets[i].insertWorker(workerID++);
    }
  }

  // Insert remainder workers into the last bucket
  while (workerID < NUM_WORKERS) {
    whichBucket[workerID] = NUM_BUCKETS - 1;
    buckets.back().insertWorker(workerID++);
  }
}

void populate_buckets_exponential(std::vector<Bucket> &buckets) {
  // Determine exponential coefficient
  int coefficient = NUM_WORKERS / ((1 << (NUM_BUCKETS + 2)) - 4);

  int workerID = 0;
  for (int i = 0; i < NUM_BUCKETS; i++) {
    int size = (4 << i) * coefficient;
    for (int j = 0; j < size; j++) {
      whichBucket[workerID] = i;
      buckets[i].insertWorker(workerID++);
    }
  }

  // Insert remainder workers into the last bucket
  while (workerID < NUM_WORKERS) {
    whichBucket[workerID] = NUM_BUCKETS - 1;
    buckets.back().insertWorker(workerID++);
  }
}

std::shared_ptr<worker_manager_service> worker_manager_service::instance =
    nullptr;
void worker_manager_service::run() {

  // Initialize all data structures
  scores = std::vector<int>(NUM_WORKERS, MAX_SCORE);
  whichBucket = std::vector<int>(NUM_WORKERS);
  buckets = std::vector<Bucket>(NUM_BUCKETS);

  switch (distribution) {
  case UNIFORM:
    populate_buckets_uniform(buckets);
    break;
  case LINEAR:
    populate_buckets_linear(buckets);
    break;
  case EXPONENTIAL:
    populate_buckets_exponential(buckets);
    break;
  }

  while (!kill) {
    sleep(WORKER_MANAGER_INTERVAL); // Alternatively, time elapsed or # of
                                    // updates, whichever comes first
    sort_worker_score();
  }
}

int worker_manager_service::sort_worker_score() {
  // Buckets keep the worker scores sorted, so we just need to output to
  // memcached Here, we split the number of workers in chunks of n (uniform
  // distribution) Can also make it a linear distribution

  int workersPerChunk = 5;
  int currentBucket = 0;
  int bucketOffset = 0;
  int currentPosition = 0;
  for (int i = 0; i <= NUM_WORKERS / workersPerChunk; i++) {
    std::stringstream s;
    for (int j = 0; j < workersPerChunk; j++) {
      s << buckets[currentBucket].getWorkerId(0) << ',';
      currentPosition++;
      bucketOffset++;
      if (bucketOffset > buckets[currentBucket].getSize()) {
        bucketOffset = 0;
        currentBucket++;
      }
    }
    // Do memcached put operation
    // memcachedObject.put(WORKER_RANKS, std:to_string(i), s.str(),std::string
    // group_key)
  }
  return 0;
}
