// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License

#include <list>
#include <map>
#include <set>
#include <string>

#include <stout/fs.hpp>
#include <stout/os.hpp>
#include <stout/path.hpp>

#include <stout/os/getcwd.hpp>
#include <stout/os/ls.hpp>
#include <stout/os/mkdir.hpp>
#include <stout/os/stat.hpp>
#include <stout/os/touch.hpp>

#include <stout/tests/utils.hpp>

using std::list;
using std::set;
using std::string;


static hashset<string> listfiles(const string& directory)
{
  hashset<string> fileset;
  Try<list<string>> entries = os::ls(directory);
  if (entries.isSome()) {
    foreach (const string& entry, entries.get()) {
      fileset.insert(entry);
    }
  }
  return fileset;
}


class RmdirTest : public TemporaryDirectoryTest {};


// TODO(hausdorff): This test is almost copy-pasted from
// `TrivialRemoveEmptyDirectoryRelativePath`; we should parameterize them to
// reduce redundancy.
TEST_F(RmdirTest, TrivialRemoveEmptyDirectoryAbsolutePath)
{
  const string tmpdir = os::getcwd();
  hashset<string> expectedListing = hashset<string>::EMPTY;

  // Directory is initially empty.
  EXPECT_EQ(expectedListing, listfiles(tmpdir));

  // Successfully make directory using absolute path.
  const string newDirectoryName = "newDirectory";
  const string newDirectoryAbsolutePath = path::join(tmpdir, newDirectoryName);
  expectedListing.insert(newDirectoryName);
  EXPECT_SOME(os::mkdir(newDirectoryAbsolutePath));
  EXPECT_EQ(expectedListing, listfiles(tmpdir));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectoryAbsolutePath));

  // Successfully remove.
  EXPECT_SOME(os::rmdir(newDirectoryAbsolutePath));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(tmpdir));
}


TEST_F(RmdirTest, TrivialRemoveEmptyDirectoryRelativePath)
{
  const string tmpdir = os::getcwd();
  hashset<string> expectedListing = hashset<string>::EMPTY;

  // Directory is initially empty.
  EXPECT_EQ(expectedListing, listfiles(tmpdir));

  // Successfully make directory using relative path.
  const string newDirectoryName = "newDirectory";
  expectedListing.insert(newDirectoryName);
  EXPECT_SOME(os::mkdir(newDirectoryName));
  EXPECT_EQ(expectedListing, listfiles(tmpdir));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectoryName));

  // Successfully remove.
  EXPECT_SOME(os::rmdir(newDirectoryName));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(tmpdir));
}


TEST_F(RmdirTest, RemoveRecursiveByDefault)
{
  const string tmpdir = os::getcwd();
  hashset<string> expectedRootListing = hashset<string>::EMPTY;
  hashset<string> expectedSubListing = hashset<string>::EMPTY;

  // Directory is initially empty.
  EXPECT_EQ(expectedRootListing, listfiles(tmpdir));

  // Successfully make directory using absolute path, and then `touch` a file
  // in that folder.
  const string newDirectoryName = "newDirectory";
  const string newDirectoryAbsolutePath = path::join(tmpdir, newDirectoryName);
  const string newFileName = "newFile";
  const string newFileAbsolutePath = path::join(
      newDirectoryAbsolutePath,
      newFileName);

  expectedRootListing.insert(newDirectoryName);
  expectedSubListing.insert(newFileName);

  EXPECT_SOME(os::mkdir(newDirectoryAbsolutePath));
  EXPECT_SOME(os::touch(newFileAbsolutePath));
  EXPECT_EQ(expectedRootListing, listfiles(tmpdir));
  EXPECT_EQ(expectedSubListing, listfiles(newDirectoryAbsolutePath));

  // Successfully remove.
  EXPECT_SOME(os::rmdir(newDirectoryAbsolutePath));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(tmpdir));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectoryAbsolutePath));
}


TEST_F(RmdirTest, TrivialFailToRemoveInvalidPath)
{
  // Directory is initially empty.
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(os::getcwd()));

  // Removing fake relative paths should error out.
  EXPECT_ERROR(os::rmdir("fakeRelativePath", false));
  EXPECT_ERROR(os::rmdir("fakeRelativePath", true));

  // Directory still empty.
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(os::getcwd()));
}


TEST_F(RmdirTest, FailToRemoveNestedInvalidPath)
{
  const string tmpdir = os::getcwd();
  hashset<string> expectedRootListing = hashset<string>::EMPTY;

  // Directory is initially empty.
  EXPECT_EQ(expectedRootListing, listfiles(tmpdir));

  // Successfully make directory using absolute path.
  const string newDirectoryName = "newDirectory";
  const string newDirectoryAbsolutePath = path::join(tmpdir, newDirectoryName);

  expectedRootListing.insert(newDirectoryName);

  EXPECT_SOME(os::mkdir(newDirectoryAbsolutePath));
  EXPECT_EQ(expectedRootListing, listfiles(tmpdir));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectoryAbsolutePath));

  // Fail to remove a path to an invalid folder inside the
  // `newDirectoryAbsolutePath`.
  const string fakeAbsolutePath = path::join(newDirectoryAbsolutePath, "fake");
  EXPECT_ERROR(os::rmdir(fakeAbsolutePath, false));
  EXPECT_EQ(expectedRootListing, listfiles(tmpdir));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectoryAbsolutePath));

  // Test the same thing, but using the `recursive` flag.
  EXPECT_ERROR(os::rmdir(fakeAbsolutePath, true));
  EXPECT_EQ(expectedRootListing, listfiles(tmpdir));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectoryAbsolutePath));
}


#ifndef __WINDOWS__
// This test verifies that `rmdir` can remove a directory with a
// device file.
// TODO(hausdorff): Port this test to Windows. It is not clear that `rdev` and
// `mknod` will implement the functionality expressed in this test, and as the
// need for these capabilities arise elsewhere in the codebase, we should
// rethink abstractions we need here, and subsequently, what this test should
// look like.
TEST_F(RmdirTest, RemoveDirectoryWithDeviceFile)
{
  // mknod requires root permission.
  Result<string> user = os::user();
  ASSERT_SOME(user);

  if (user.get() != "root") {
    return;
  }

  // Create a 'char' device file with major number same as that of
  // `/dev/null`.
  const string deviceDirectory = path::join(os::getcwd(),
      "deviceDirectory");
  ASSERT_SOME(os::mkdir(deviceDirectory));

  const string device = "null";

  const string existing = path::join("/dev", device);
  ASSERT_TRUE(os::exists(existing));

  Try<mode_t> mode = os::stat::mode(existing);
  ASSERT_SOME(mode);

  Try<dev_t> rdev = os::stat::rdev(existing);
  ASSERT_SOME(rdev);

  const string another = path::join(deviceDirectory, device);
  ASSERT_FALSE(os::exists(another));

  EXPECT_SOME(os::mknod(another, mode.get(), rdev.get()));

  EXPECT_SOME(os::rmdir(deviceDirectory));
}
#endif // __WINDOWS__


// This test verifies that `rmdir` can remove a directory with a
// symlink that has no target.
TEST_F(RmdirTest, RemoveDirectoryWithNoTargetSymbolicLink)
{
  const string newDirectory = path::join(os::getcwd(), "newDirectory");
  ASSERT_SOME(os::mkdir(newDirectory));

  const string link = path::join(newDirectory, "link");

  // Create a symlink to non-existent file 'tmp'.
  ASSERT_SOME(fs::symlink("tmp", link));

  EXPECT_SOME(os::rmdir(newDirectory));
}


// This test verifies that `rmdir` will only remove the symbolic link and not
// the target directory.
TEST_F(RmdirTest, RemoveDirectoryWithSymbolicLinkTargetDirectory)
{
  const string newDirectory = path::join(os::getcwd(), "newDirectory");
  ASSERT_SOME(os::mkdir(newDirectory));

  const string link = path::join(newDirectory, "link");

  const string targetDirectory = path::join(os::getcwd(), "targetDirectory");

  ASSERT_SOME(os::mkdir(targetDirectory));

  // Create a symlink that targets a directory outside the 'newDirectory'.
  ASSERT_SOME(fs::symlink(targetDirectory, link));

  EXPECT_SOME(os::rmdir(newDirectory));

  // Verify that the target directory is not removed.
  ASSERT_TRUE(os::exists(targetDirectory));
}


// This test verifies that `rmdir` will only remove the symbolic link and not
// the target file.
TEST_F(RmdirTest, RemoveDirectoryWithSymbolicLinkTargetFile)
{
  const string newDirectory = path::join(os::getcwd(), "newDirectory");
  ASSERT_SOME(os::mkdir(newDirectory));

  const string link = path::join(newDirectory, "link");

  const string targetFile = path::join(os::getcwd(), "targetFile");

  ASSERT_SOME(os::touch(targetFile));

  // Create a symlink that targets a file outside the 'newDirectory'.
  ASSERT_SOME(fs::symlink(targetFile, link));

  EXPECT_SOME(os::rmdir(newDirectory));

  // Verify that the target file is not removed.
  ASSERT_TRUE(os::exists(targetFile));
}


// This tests that when appropriately instructed, `rmdir` can remove
// the files and subdirectories that appear in a directory but
// preserve the directory itself.
TEST_F(RmdirTest, RemoveDirectoryButPreserveRoot)
{
  const string newDirectory = path::join(os::getcwd(), "newDirectory");
  ASSERT_SOME(os::mkdir(newDirectory));

  const string subDirectory = path::join(newDirectory, "subDirectory");
  ASSERT_SOME(os::mkdir(subDirectory));

  const string file1 = path::join(newDirectory, "file1");
  ASSERT_SOME(os::touch(file1));

  const string file2 = path::join(subDirectory, "file2");
  ASSERT_SOME(os::touch(file2));

  EXPECT_SOME(os::rmdir(newDirectory, true, false));
  EXPECT_TRUE(os::exists(newDirectory));
  EXPECT_EQ(hashset<string>::EMPTY, listfiles(newDirectory));
}
