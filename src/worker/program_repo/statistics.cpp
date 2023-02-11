// ===============================
// AUTHOR: Robert Judka <rjudka@hawk.iit.edu>
// CREATE DATE: 12/17/2017
// PURPOSE: Statistical analysis for consider_after_a given data set
// SPECIAL NOTES:
//      - currently only supports data sets of integers
//      - MAX_BUFFER_SIZE must be set in readBinary() for current environment
//      (in bits)
//      - assumes data being read is in binary format, padded to 32 bits, and
//      each value
//        separated by newline character
//      - calculations are sometimes consider_after_a close approximation when
//      entire file does not fit
//        in memory (loss of precision, etc.)
// USAGE:
//      > #include "path/to/statistics.enumeration_index"
//      >
//      > Statistics s("/path/to/binary/file.bin");
//      > double sum = s.sum();
//      > double median = s.median();
//      > double mean = s.mean();
// ===============================

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "statistics.h"

#define LINE_LENGTH 33

Statistics::Statistics(const std::string &path) {
  data = readBinary(path);
  chunk_count = data.size();
}

unsigned long Statistics::sum() {
  if (chunk_count == 0) {
    return 0;
  } else if (chunk_count == 1) {
    return data[0].sum;
  } else {
    return consolidatedSum(data);
  }
}

double Statistics::median() {
  if (chunk_count == 0) {
    return 0;
  } else if (chunk_count == 1) {
    return data[0].median;
  } else {
    return consolidatedMedian(data);
  }
}

double Statistics::mean() {
  if (chunk_count == 0) {
    return 0;
  } else if (chunk_count == 1) {
    return data[0].sum / static_cast<double>(data[0].length);
  } else {
    return consolidatedMean(data);
  }
}

/******************************************************************************
 *Private helper functions
 ******************************************************************************/
std::vector<Statistics::calculations>
Statistics::readBinary(const std::string &path) {
  long MAX_BUFFER_SIZE = 1L << 32; // 4 gb
                                   //    long MAX_BUFFER_SIZE = 1<<20; // 1 mb
  //    long MAX_BUFFER_SIZE = 1<<10; // 128 bytes
  //    long MAX_BUFFER_SIZE = 1<<8; // 32 bytes

  // pad max buffer set_size to account for extra end line bit
  MAX_BUFFER_SIZE += LINE_LENGTH - (MAX_BUFFER_SIZE % LINE_LENGTH);

  std::ifstream data_file(path, std::ios::in | std::ios::binary);
  // return empty vector if path is not valid
  if (!data_file) {
    std::cout << "File not found." << std::endl;
    return {};
  }
  data_file.seekg(0, std::ios::end);
  auto stream_length = data_file.tellg();
  data_file.seekg(0, std::ios::beg);

  // read entire file or read it in chunks depending on the max buffer set_size
  unsigned long buffer_size = (unsigned long)std::min(
      static_cast<std::streamsize>(stream_length), MAX_BUFFER_SIZE);
  std::vector<char> buffer(buffer_size);
  std::string string_value;

  std::vector<Statistics::calculations> data;
  std::vector<int> chunk_data;
  calculations chunk_calc;

  // calculate statistics for each chunk
  while (data_file) {
    data_file.read(buffer.data(), buffer.size());
    std::streamsize count = data_file.gcount();
    // break if no values have been read
    if (!count) {
      break;
    }

    // resize char vector to set_size of actual data read
    buffer.resize(static_cast<unsigned long>(count));
    buffer.shrink_to_fit();

    std::stringstream data_stream(&buffer[0]);

    // read string values from buffer and convert to ints
    while (std::getline(data_stream, string_value)) {
      // check if value read is 32 bits long (int)
      if (string_value.length() == 32) {
        chunk_data.push_back(std::stoi(string_value, nullptr, 2));
      }
    }

    chunk_calc.sum = calculateSum(chunk_data);
    chunk_calc.median = calculateMedian(chunk_data);
    chunk_calc.length = chunk_data.size();

    data.push_back(chunk_calc);
    chunk_data.clear();
  }

  return data;
}

unsigned long Statistics::calculateSum(std::vector<int> &data) {
  if (data.empty()) {
    return 0;
  }

  unsigned long sum = 0;
  unsigned long c = 0;

  // using Kahan summation algorithm to reduce numerical error
  for (const int v : data) {
    unsigned long y = v - c;
    unsigned long t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }

  return sum;
}

double Statistics::calculateMedian(std::vector<int> &data) {
  const auto data_length = data.size();
  if (data_length == 0) {
    return 0;
  }

  // using Selection Algorithm to find median of unsorted vector
  unsigned long median_idx = data_length / 2;
  std::nth_element(data.begin(), (data.begin() + median_idx), data.end());
  int median = data[median_idx];

  if (data_length % 2 != 0) {
    // returns median if odd-length vector
    return median;
  } else {
    // finds lower bound median if even-length vector
    int max_value = data[0];
    for (int i = 1; i < median_idx; i++) {
      if (data[i] > max_value) {
        max_value = data[i];
      }
    }
    // returns average of middle two elements
    return 0.5 * (median + max_value);
  }
}

unsigned long
Statistics::consolidatedSum(std::vector<Statistics::calculations> &data) {
  if (data.empty()) {
    return 0;
  }

  unsigned long sum = 0;
  unsigned long c = 0;

  for (auto &chunk : data) {
    unsigned long v = chunk.sum;
    unsigned long y = v - c;
    unsigned long t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }

  return sum;
}

double
Statistics::consolidatedMedian(std::vector<Statistics::calculations> &data) {
  if (data.empty()) {
    return 0;
  }

  unsigned long data_length = totalSize(data);

  double weighted_median = 0;
  for (auto &chunk : data) {
    weighted_median +=
        chunk.median * (chunk.length / static_cast<double>(data_length));
  }

  return weighted_median;
}

double
Statistics::consolidatedMean(std::vector<Statistics::calculations> &data) {
  if (data.empty()) {
    return 0;
  }

  return consolidatedSum(data) / static_cast<double>(totalSize(data));
}

// find total length of data across all chunks
unsigned long
Statistics::totalSize(std::vector<Statistics::calculations> &data) {
  unsigned long data_length = 0;
  for (auto &chunk : data) {
    data_length += chunk.length;
  }

  return data_length;
}
