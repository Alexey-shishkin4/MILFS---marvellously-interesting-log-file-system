#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

int cli_main();
static const fs::path kMilfsImage = "milfs.img";


namespace fs = std::filesystem;

// Pair: input_file <-> expected_output_file
struct IoFilePair {
    std::string input;
    std::string expected;
};

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

// collect all pairs input/output
std::vector<IoFilePair> collect_tests(const std::string& tests_dir, const std::string& extension_in, const std::string& extension_out) {
    std::vector<IoFilePair> pairs;

    for (const auto& in_entry : fs::recursive_directory_iterator(tests_dir)) {
        if (!in_entry.is_regular_file()) continue;
        
        fs::path in_path = in_entry.path();

        if (in_path.extension() != extension_in) continue;

        fs::path out_path = in_path;
        out_path.replace_extension(extension_out);

        if (!fs::exists(out_path)) {
            ADD_FAILURE() << "No output file \"" << out_path.string()
                          << "\" for input \"" << in_path.string() << "\"";
            continue;
        }

        pairs.push_back({in_path.string(), out_path.string()});
    }

    std::sort(pairs.begin(), pairs.end(), 
              [](const IoFilePair& a, const IoFilePair& b) {
                return a.input < b.input;
                }
            );
    return pairs;
}

class CLITest : public ::testing::TestWithParam<IoFilePair> {
protected:
    void SetUp() override {
        std::error_code ec;
        fs::remove(kMilfsImage, ec);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(kMilfsImage, ec);
    }
};

TEST_P(CLITest, RunCLI) {
    IoFilePair param = GetParam();

    std::string input_str = read_file(param.input);
    std::string expected  = read_file(param.expected);

    std::istringstream input(input_str);
    std::ostringstream output;
    
    auto* old_cin = std::cin.rdbuf(input.rdbuf());
    auto* old_cout = std::cout.rdbuf(output.rdbuf());

    cli_main();

    std::cin.rdbuf(old_cin);
    std::cout.rdbuf(old_cout);

    EXPECT_EQ(output.str(), expected);
}

void PrintTo(const IoFilePair& pair, ::std::ostream* os) {
    *os << fs::path(pair.input).stem().string();
}

// Automatic generating tests input/output
INSTANTIATE_TEST_SUITE_P(
    CliTests,
    CLITest,
    ::testing::ValuesIn(
        collect_tests(
            MILFS_TESTS_DIR,            // look test/CMakeLists.txt
            MILFS_TESTS_EXTENSION_IN,   // look test/CMakeLists.txt
            MILFS_TESTS_EXTENSION_OUT   // look test/CMakeLists.txt
        )
    )
);



int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
