#include "testenv.hpp"

#include <boost/regex.hpp>
#include <boost/program_options.hpp>

using namespace boost::program_options;

TEST(cpu_list_test, simple) {
  CpuList cpu_list("3");

  EXPECT_EQ(cpu_list.cpus.size(), 1);
  EXPECT_TRUE((cpu_list.cpus == std::vector<int>{3}));
  SUCCEED();
}

TEST(cpu_list_test, range) {
  CpuList cpu_list("5-8");

  EXPECT_EQ(cpu_list.cpus.size(), 4);
  EXPECT_TRUE((cpu_list.cpus == std::vector<int>{5, 6, 7, 8}));
  SUCCEED();
}

TEST(cpu_list_test, list) {
  CpuList cpu_list("3,5,6");

  EXPECT_EQ(cpu_list.cpus.size(), 3);
  EXPECT_TRUE((cpu_list.cpus == std::vector<int>{3, 5, 6}));
  SUCCEED();
}

TEST(cpu_list_test, list_of_ranges) {
  CpuList cpu_list("3-5,6-7,9-12");

  EXPECT_EQ(cpu_list.cpus.size(), 9);
  EXPECT_TRUE((cpu_list.cpus ==
               std::vector<int>{3, 4, 5, 6, 7, 9, 10, 11, 12}));
  SUCCEED();
}

TEST(cpu_list_test, mixed_list) {
  CpuList cpu_list("3-5,1,6-7,9-12,15");

  EXPECT_EQ(cpu_list.cpus.size(), 11);
  EXPECT_TRUE((cpu_list.cpus ==
               std::vector<int>{1, 3, 4, 5, 6, 7, 9, 10, 11, 12, 15}));
  SUCCEED();
}

TEST(cpu_list_test, duplicates) {
  CpuList cpu_list("3-5,1,5-6,3-7,15");

  EXPECT_EQ(cpu_list.cpus.size(), 7);
  EXPECT_TRUE((cpu_list.cpus ==
               std::vector<int>{1, 3, 4, 5, 6, 7, 15}));
  SUCCEED();
}

TEST(cpu_list_test, exceptions_test) {
  EXPECT_THROW(CpuList("sth"),  validation_error);
  EXPECT_THROW(CpuList("a"),  validation_error);
  EXPECT_THROW(CpuList("a-"),  validation_error);
  EXPECT_THROW(CpuList("3-"),  validation_error);
  EXPECT_THROW(CpuList("5-3"),  validation_error);
  EXPECT_THROW(CpuList("5-3-"),  validation_error);
  EXPECT_THROW(CpuList("5-3-7"),  validation_error);
  EXPECT_THROW(CpuList("-3"),  validation_error);

  EXPECT_THROW(CpuList("5-3,a"),  validation_error);
  EXPECT_THROW(CpuList("5-3,3-"),  validation_error);
  EXPECT_THROW(CpuList("5-3,-6"),  validation_error);
  EXPECT_THROW(CpuList("5-3,"),  validation_error);
  EXPECT_THROW(CpuList("5-3,8-7"),  validation_error);

  EXPECT_THROW(CpuList(",3-5,7-8"),  validation_error);

  EXPECT_NO_THROW(CpuList("5"));
  EXPECT_NO_THROW(CpuList("5,6,7"));
  EXPECT_NO_THROW(CpuList("5,7,6"));
  EXPECT_NO_THROW(CpuList("all"));
  SUCCEED();
}


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  auto result = RUN_ALL_TESTS();

  return result;
}
