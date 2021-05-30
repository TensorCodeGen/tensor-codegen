#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <Eigen/Core>

#define SAMPLE_SIZE 200

// Will be 200x200 but eigen disallows static sizes that are too large
using Mat = Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic>;

// Returns time in seconds
double bench() {
  namespace chrono = std::chrono;
  Mat testMatsLeft[SAMPLE_SIZE];
  Mat testMatsRight[SAMPLE_SIZE];
  std::vector<Mat> results;

  // Initialization
  results.reserve(SAMPLE_SIZE);
  for (size_t i = 0; i < SAMPLE_SIZE; i++) {
    testMatsLeft[i] = Mat::Random(200, 200);
    testMatsRight[i] = Mat::Random(200, 200);
  }

  // Bench and measure
  auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < SAMPLE_SIZE; i++) {
    results[i] = testMatsLeft[i] * testMatsRight[i];
  }
  auto diff = chrono::steady_clock::now() - start;

  return chrono::duration<double>(diff).count();
}

int main(int argc, char **argv) {
  if (argc > 1)
    Eigen::setNbThreads(atoi(argv[1]));
  else
    Eigen::setNbThreads(1);
  std::cout << "Threads: " << Eigen::nbThreads() << std::endl;
  std::cout << "Time: " << bench() << std::endl;
  return 0;
}
