/**
* @file cartesian_product.test.cpp
* @brief 
* @author Rui Dong
* @date 5/25/24
*/

#include <gtest/gtest.h>
#include <foresighter/common/cartesian_product.hpp>

using namespace foresighter::common;

class MyInt {
 public:
  MyInt() = default;

  MyInt(int value) : value(value) {}

  int getValue() const { return value; }

 private:
  int value;
};

TEST(Common, CartesianProductVector_Normal) {
  //test case:
  //input: {{1,2}, {3,4,5}, {6,7}}
  //output:
  //{{1,3,6}, {1,3,7}, {1,4,6}, {1,4,7}, {1,5,6}, {1,5,7},
  // {2,3,6}, {2,3,7}, {2,4,6}, {2,4,7}, {2,5,6}, {2,5,7}}
  std::vector<std::vector<MyInt>> input = {{MyInt(1), MyInt(2)},
                                           {MyInt(3), MyInt(4), MyInt(5)},
                                           {MyInt(6), MyInt(7)}};
  auto output = cartesianProduct(input);
  EXPECT_EQ(output.size(), 12);
  EXPECT_EQ(output[0].size(), 3);
  EXPECT_EQ(output[0][0].getValue(), 1);
  EXPECT_EQ(output[0][1].getValue(), 3);
  EXPECT_EQ(output[0][2].getValue(), 6);
  EXPECT_EQ(output[1][0].getValue(), 1);
  EXPECT_EQ(output[1][1].getValue(), 3);
  EXPECT_EQ(output[1][2].getValue(), 7);
  EXPECT_EQ(output[2][0].getValue(), 1);
  EXPECT_EQ(output[2][1].getValue(), 4);
  EXPECT_EQ(output[2][2].getValue(), 6);
  EXPECT_EQ(output[3][0].getValue(), 1);
  EXPECT_EQ(output[3][1].getValue(), 4);
  EXPECT_EQ(output[3][2].getValue(), 7);
  EXPECT_EQ(output[4][0].getValue(), 1);
  EXPECT_EQ(output[4][1].getValue(), 5);
  EXPECT_EQ(output[4][2].getValue(), 6);
  EXPECT_EQ(output[5][0].getValue(), 1);
  EXPECT_EQ(output[5][1].getValue(), 5);
  EXPECT_EQ(output[5][2].getValue(), 7);
  EXPECT_EQ(output[6][0].getValue(), 2);
  EXPECT_EQ(output[6][1].getValue(), 3);
  EXPECT_EQ(output[6][2].getValue(), 6);
  EXPECT_EQ(output[7][0].getValue(), 2);
  EXPECT_EQ(output[7][1].getValue(), 3);
  EXPECT_EQ(output[7][2].getValue(), 7);
  EXPECT_EQ(output[8][0].getValue(), 2);
  EXPECT_EQ(output[8][1].getValue(), 4);
  EXPECT_EQ(output[8][2].getValue(), 6);
  EXPECT_EQ(output[9][0].getValue(), 2);
  EXPECT_EQ(output[9][1].getValue(), 4);
  EXPECT_EQ(output[9][2].getValue(), 7);
  EXPECT_EQ(output[10][0].getValue(), 2);
  EXPECT_EQ(output[10][1].getValue(), 5);
  EXPECT_EQ(output[10][2].getValue(), 6);
  EXPECT_EQ(output[11][0].getValue(), 2);
  EXPECT_EQ(output[11][1].getValue(), 5);
  EXPECT_EQ(output[11][2].getValue(), 7);
}

TEST(Common, CartesianProductVector_Empty) {
  //test case:
  //input: {{1, 2}, {}, {3, 4}}
  //output: {}
  std::vector<std::vector<MyInt>> input = {
      {MyInt(1), MyInt(2)}, {}, {MyInt(3), MyInt(4)}};
  auto output = cartesianProduct(input);
  EXPECT_EQ(output.size(), 0);
}

TEST(Common, CartesianProductIter_Normal) {
  //test case:
  //input: {{1,2}, {3,4,5}, {6,7}}
  //output:
  //{{1,3,6}, {1,3,7}, {1,4,6}, {1,4,7}, {1,5,6}, {1,5,7},
  // {2,3,6}, {2,3,7}, {2,4,6}, {2,4,7}, {2,5,6}, {2,5,7}}
  std::vector<std::vector<MyInt>> input = {{MyInt(1), MyInt(2)},
                                           {MyInt(3), MyInt(4), MyInt(5)},
                                           {MyInt(6), MyInt(7)}};
  CartesianProductIterator<MyInt> iter(input);
  EXPECT_EQ(iter.size(), 12);
  EXPECT_TRUE(iter.hasNext());
  auto output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 1);
  EXPECT_EQ(output[1].getValue(), 3);
  EXPECT_EQ(output[2].getValue(), 6);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 1);
  EXPECT_EQ(output[1].getValue(), 3);
  EXPECT_EQ(output[2].getValue(), 7);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 1);
  EXPECT_EQ(output[1].getValue(), 4);
  EXPECT_EQ(output[2].getValue(), 6);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 1);
  EXPECT_EQ(output[1].getValue(), 4);
  EXPECT_EQ(output[2].getValue(), 7);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 1);
  EXPECT_EQ(output[1].getValue(), 5);
  EXPECT_EQ(output[2].getValue(), 6);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 1);
  EXPECT_EQ(output[1].getValue(), 5);
  EXPECT_EQ(output[2].getValue(), 7);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 2);
  EXPECT_EQ(output[1].getValue(), 3);
  EXPECT_EQ(output[2].getValue(), 6);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 2);
  EXPECT_EQ(output[1].getValue(), 3);
  EXPECT_EQ(output[2].getValue(), 7);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 2);
  EXPECT_EQ(output[1].getValue(), 4);
  EXPECT_EQ(output[2].getValue(), 6);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 2);
  EXPECT_EQ(output[1].getValue(), 4);
  EXPECT_EQ(output[2].getValue(), 7);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 2);
  EXPECT_EQ(output[1].getValue(), 5);
  EXPECT_EQ(output[2].getValue(), 6);
  EXPECT_TRUE(iter.hasNext());
  output = iter.next();
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].getValue(), 2);
  EXPECT_EQ(output[1].getValue(), 5);
  EXPECT_EQ(output[2].getValue(), 7);
  EXPECT_FALSE(iter.hasNext());
}

TEST(Common, CartesianProductIter_Empty) {
  //test case:
  //input: {{1, 2}, {}, {3, 4}}
  //output: {}
  std::vector<std::vector<MyInt>> input = {
      {MyInt(1), MyInt(2)}, {}, {MyInt(3), MyInt(4)}};
  CartesianProductIterator<MyInt> iter(input);
  EXPECT_EQ(iter.size(), 0);
  EXPECT_FALSE(iter.hasNext());
}

TEST(Common, CartisianProductVector_CompletelyEmpty) {
  std::vector<std::vector<MyInt>> input = {};
  auto output = cartesianProduct(input);
  EXPECT_EQ(output.size(), 0);
}

TEST(Common, CartisianProductIter_CompletelyEmpty) {
  std::vector<std::vector<MyInt>> input = {};
  CartesianProductIterator<MyInt> iter(input);
  EXPECT_EQ(iter.size(), 0);
  EXPECT_FALSE(iter.hasNext());
}
