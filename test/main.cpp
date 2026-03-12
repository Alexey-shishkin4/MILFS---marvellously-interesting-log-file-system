#include <gtest/gtest.h>
#include "../src/cli.cpp"


class ParserCommandTest : public ::testing::Test  {
public:
    std::string st = "";
    std::vector<std::string> input = {};
    std::vector<std::string> output = {};

    void concatenateArr() {
        if (input.empty()) { return; }
        
        st = input[0];
        for (size_t i = 1; i < input.size(); i++) { st += " " + input[i]; }
    }

    void getTokens() {
        concatenateArr();
        output = parseCommand(st);
    }
};


TEST_F(ParserCommandTest, EmptyStringTest_01) {
    input = {""};
    getTokens();
    EXPECT_TRUE(output.empty());
}

TEST_F(ParserCommandTest, EmptyStringTest_02) {
    input = {" "};
    getTokens();
    EXPECT_TRUE(output.empty());
}

TEST_F(ParserCommandTest, EmptyStringTest_03) {
    input = {"    ", " ", "", "                              "};
    getTokens();
    EXPECT_TRUE(output.empty());
}

TEST_F(ParserCommandTest, EmptyStringTest_04) {
    input = {"    ", " ", "", "                              ", "\n \t \n \n \n"};
    getTokens();
    EXPECT_TRUE(output.empty());
}

TEST_F(ParserCommandTest, QuotesTest_01) {
    input = {"\"zxc\""};
    getTokens();

    EXPECT_FALSE(output.empty());
    EXPECT_EQ(output, input);
}

TEST_F(ParserCommandTest, QuotesTest_02) {
    input = {"\" zxc  \""};
    getTokens();

    EXPECT_FALSE(output.empty());
    EXPECT_EQ(output, input);
}

TEST_F(ParserCommandTest, OneTokenTest_01) {
    input = {"mkdir"};
    getTokens();

    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output, input);
}

TEST_F(ParserCommandTest, OneTokenTest_02) {
    input = {"mkdir", ""};
    std::vector<std::string> answer = {input[0]};
    getTokens();

    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output, answer);
}

TEST_F(ParserCommandTest, OneTokenTest_03) {
    input = {"\n", "mkdir", "   "};
    std::vector<std::string> answer = {input[1]};
    getTokens();

    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output, answer);
}


TEST_F(ParserCommandTest, MoreTokenTest_01) {
    input = {"\n", "mkdir", "zxc"};
    std::vector<std::string> answer = {input[1], input[2]};
    getTokens();

    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output, answer);
}

TEST_F(ParserCommandTest, MoreTokenTest_02) {
    input = {"\n", "mkdir", "zxc", "\'axaxaxaxax\'"};
    std::vector<std::string> answer = {input[1], input[2], input[3]};
    getTokens();

    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output, answer);
}


int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
