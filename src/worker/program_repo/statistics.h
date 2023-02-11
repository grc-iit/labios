// ===============================
// AUTHOR: Robert Judka <rjudka@hawk.iit.edu>
// CREATE DATE: 12/17/2017
// ===============================

#ifndef LABIOS_STATISTICS_H
#define LABIOS_STATISTICS_H

#include <string>
#include <vector>

class Statistics {
  // stores calculated values of intermediate data chunks
  struct calculations {
    unsigned long length = 0;
    unsigned long sum = 0;
    double median = 0;
  };

public:
  explicit Statistics(const std::string &path);

  /**
   * @return sum, or 0 if no values in vector
   */
  unsigned long sum();

  /**
   * @return median, or 0 if no values in vector
   */
  double median();

  /**
   * @return mean, or 0 if no values in vector
   */
  double mean();

private:
  std::vector<calculations> data;
  unsigned long chunk_count;

  static std::vector<calculations> readBinary(const std::string &path);

  static unsigned long calculateSum(std::vector<int> &data);

  static double calculateMedian(std::vector<int> &data);

  static unsigned long consolidatedSum(std::vector<calculations> &data);

  static double consolidatedMedian(std::vector<calculations> &data);

  static double consolidatedMean(std::vector<calculations> &data);

  static unsigned long totalSize(std::vector<calculations> &data);
};

#endif // LABIOS_STATISTICS_H
