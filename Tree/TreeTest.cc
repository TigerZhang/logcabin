/* Copyright (c) 2012 Stanford University
 * Copyright (c) 2014 Diego Ongaro
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>

#include "build/Protocol/Client.pb.h"
#include "Core/StringUtil.h"
#include "Tree/Tree.h"
#include "Storage/FilesystemUtil.h"
#include "Storage/Layout.h"
#include "Storage/SnapshotFile.h"

namespace LogCabin {
namespace Tree {
namespace {

#define EXPECT_OK(c) do { \
    Result result = (c); \
    EXPECT_EQ(Status::OK, result.status) << result.error; \
} while (0)

#if 0
const char* ardb_conf =
        "home  data/server1/fsm-ardb"
                "daemonize no"
                "pidfile ${ARDB_HOME}/ardb.pid"
                "thread-pool-size              4"
                "server[0].listen              0.0.0.0:16379"
                "qps-limit-per-host                  0"
                "qps-limit-per-connection            0"
                "rocksdb.compaction           OptimizeLevelStyleCompaction"
                "rocksdb.scan-total-order              false"
                "rocksdb.disableWAL            true"
                "rocksdb.options               write_buffer_size=512M;max_write_buffer_number=5;min_write_buffer_number_to_merge=3;compression=kSnappyCompression;\"
                "                              bloom_locality=1;memtable_prefix_bloom_size_ratio=0.1;\"
                "                              block_based_table_factory={block_cache=512M;filter_policy=bloomfilter:10:true};\"
                "                              create_if_missing=true;max_open_files=10000;rate_limiter_bytes_per_sec=50M"
                "leveldb.options               block_cache_size=512M,write_buffer_size=128M,max_open_files=5000,block_size=4k,block_restart_interval=16,\"
                "                              bloom_bits=10,compression=snappy,logenable=yes"
                ""
                "lmdb.options                  database_maxsize=10G,database_maxdbs=4096,readahead=no,batch_commit_watermark=1024"
                "perconaft.options              cache_size=128M,compression=snappy"
                "wiredtiger.options            cache_size=512M,session_max=8k,chunk_size=100M,block_size=4k,bloom_bits=10,\"
                "                              mmap=false,compressor=snappy"
                ""
                "forestdb.options              chunksize=8,blocksize=4K"
                "timeout 0"
                "tcp-keepalive 0"
                "loglevel info"
                "logfile  stdout"
                "data-dir ${ARDB_HOME}/data"
                "slave-workers   2"
                "max-slave-worker-queue  1024"
                "repl-dir                          ${ARDB_HOME}/repl"
                "slave-serve-stale-data yes"
                "slave-priority 100"
                "slave-read-only yes"
                "backup-dir                        ${ARDB_HOME}/backup"
                "backup-file-format                ardb"
                "repl-disable-tcp-nodelay no"
                "repl-backlog-size           1G"
                "repl-backlog-cache-size     100M"
                "snapshot-max-lag-offset     500M"
                "maxsnapshots                10"
                "slave-serve-stale-data yes"
                "slave-cleardb-before-fullresync    yes"
                "repl-backlog-sync-period         5"
                "slave-ignore-expire   no"
                "slave-ignore-del      no"
                "cluster-name   ardb-cluster"
                "slave-client-output-buffer-limit 256mb"
                "pubsub-client-output-buffer-limit 32mb"
                "slowlog-log-slower-than 10000"
                "slowlog-max-len 128"
                "lua-time-limit 5000"
                "hll-sparse-max-bytes 3000"
                "compact-after-snapshot-load  false"
                "scan-redis-compatible         yes"
                "scan-cursor-expire-after      60"
                "redis-compatible-mode     no"
                "redis-compatible-version  2.8.0"
                "statistics-log-period     600"
                "range-delete-min-size  100"
;
#endif

void
dumpTreeHelper(const Tree& tree,
               std::string path,
               std::vector<std::string>& nodes)
{
    nodes.push_back(path);

    std::vector<std::string> children;
    EXPECT_OK(tree.listDirectory(path, children));
    for (auto it = children.begin();
         it != children.end();
         ++it) {
        if (Core::StringUtil::endsWith(*it, "/")) {
            dumpTreeHelper(tree, path + *it, nodes);
        } else {
            nodes.push_back(path + *it);
        }
    }
}

std::string
dumpTree(const Tree& tree)
{
    std::vector<std::string> nodes;
    dumpTreeHelper(tree, "/", nodes);
    std::string ret;
    for (size_t i = 0; i < nodes.size(); ++i) {
        ret += nodes.at(i);
        if (i < nodes.size() - 1)
            ret += " ";
    }
    return ret;
}


//TEST(TreeFileTest, findLatestSnapshot)
//{
//    Tree tree;
//    tree.findLatestSnapshot(NULL);
//}


TEST(TreeDirectoryTest, dumpSnapshot)
{
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    long now = timeSpec.tv_sec;
    Storage::Layout layout;
    layout.initTemporary();

    Tree tree;
    tree.Init(layout.topDir.path);
    tree.makeDirectory("/a");
    tree.makeDirectory("/a/b");
    tree.makeDirectory("/a/b/c");
    tree.makeDirectory("/a/d");
    tree.makeDirectory("/e");
    tree.makeDirectory("/f");
    tree.makeDirectory("/f/h");
    tree.write("/f/g", "rawr", now);

    {
#ifdef MEM_FSM
        Storage::SnapshotFile::Writer writer(layout);
        tree.superRoot.dumpSnapshot(writer);
        writer.save();
#endif // MEM_FSM
    }
    {
#ifdef MEM_FSM
        Storage::SnapshotFile::Reader reader(layout);
        Tree t2;
        t2.superRoot.loadSnapshot(reader);
        EXPECT_EQ(dumpTree(tree), dumpTree(t2));
#endif // MEM_FSM
    }
}


const std::string ardb_conf =
        "home  data\n"
                "daemonize no\n"
                "pidfile ${ARDB_HOME}/ardb.pid\n"
                "thread-pool-size              4\n"
                "server[0].listen              0.0.0.0:16379\n"
                "qps-limit-per-host                  0\n"
                "qps-limit-per-connection            0\n"
                "rocksdb.compaction           OptimizeLevelStyleCompaction\n"
                "rocksdb.scan-total-order              false\n"
                "rocksdb.disableWAL            false\n"
                "rocksdb.options               write_buffer_size=512M;max_write_buffer_number=5;min_write_buffer_number_to_merge=3;compression=kSnappyCompression;\\\n"
                "bloom_locality=1;memtable_prefix_bloom_size_ratio=0.1;\\\n"
                "block_based_table_factory={block_cache=512M;filter_policy=bloomfilter:10:true};\\\n"
                "create_if_missing=true;max_open_files=10000;rate_limiter_bytes_per_sec=50M\n"
                "leveldb.options               block_cache_size=512M,write_buffer_size=128M,max_open_files=5000,block_size=4k,block_restart_interval=16,\\\n"
                "bloom_bits=10,compression=snappy,logenable=yes\n"
                "\n"
                "lmdb.options                  database_maxsize=10G,database_maxdbs=4096,readahead=no,batch_commit_watermark=1024\n"
                "perconaft.options              cache_size=128M,compression=snappy\n"
                "wiredtiger.options            cache_size=512M,session_max=8k,chunk_size=100M,block_size=4k,bloom_bits=10,\\\n"
                "mmap=false,compressor=snappy\n"
                "\n"
                "forestdb.options              chunksize=8,blocksize=4K\n"
                "timeout 0\n"
                "tcp-keepalive 0\n"
                "loglevel info\n"
                "logfile  /tmp/ardb.log\n"
                "data-dir ${ARDB_HOME}/data\n"
                "slave-workers   2\n"
                "max-slave-worker-queue  1024\n"
                "repl-dir                          ${ARDB_HOME}/repl\n"
                "slave-serve-stale-data yes\n"
                "slave-priority 100\n"
                "slave-read-only yes\n"
                "backup-dir                        ${ARDB_HOME}/backup\n"
                "backup-file-format                ardb\n"
                "repl-disable-tcp-nodelay no\n"
                "repl-backlog-size           1G\n"
                "repl-backlog-cache-size     100M\n"
                "snapshot-max-lag-offset     500M\n"
                "maxsnapshots                10\n"
                "slave-serve-stale-data yes\n"
                "slave-cleardb-before-fullresync    yes\n"
                "repl-backlog-sync-period         5\n"
                "slave-ignore-expire   no\n"
                "slave-ignore-del      no\n"
                "cluster-name   ardb-cluster\n"
                "slave-client-output-buffer-limit 256mb\n"
                "pubsub-client-output-buffer-limit 32mb\n"
                "slowlog-log-slower-than 10000\n"
                "slowlog-max-len 128\n"
                "lua-time-limit 5000\n"
                "hll-sparse-max-bytes 3000\n"
                "compact-after-snapshot-load  false\n"
                "scan-redis-compatible         yes\n"
                "scan-cursor-expire-after      60\n"
                "redis-compatible-mode     no\n"
                "redis-compatible-version  2.8.0\n"
                "statistics-log-period     600\n"
                "range-delete-min-size  100\n"
;

class TreeTreeTest : public ::testing::Test {
    TreeTreeTest()
        : tree(), layout()
    {
        layout.initTemporary();
        tree.Init(layout.topDir.path);

        EXPECT_EQ("/", dumpTree(tree));

        std::ofstream myfile;
        myfile.open (layout.topDir.path + "/ardb.conf");
        myfile << ardb_conf;
        myfile.close();

    }

    Storage::Layout layout;
    Tree tree;
};

TEST_F(TreeTreeTest, dumpSnapshot)
{
    {
        auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
        long now = timeSpec.tv_sec;
        Storage::SnapshotFile::Writer writer(layout);

//        tree.startSnapshot(0);
        tree.write("/c", "foo", now);
        tree.dumpSnapshot(writer);
        writer.save();
    }
    /*
    tree.removeFile("/c");
    tree.write("/d", "bar");
    {
        Storage::SnapshotFile::Reader reader(layout);
        tree.loadSnapshot(reader);
    }
     */
    std::vector<std::string> children;
    EXPECT_OK(tree.listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{ "c" }), children);
}


TEST_F(TreeTreeTest, normalLookup)
{
    std::string contents;
    Result result;
    result = tree.read("/a/b", contents);
    EXPECT_EQ(Status::LOOKUP_ERROR, result.status);
//    EXPECT_EQ("Parent /a of /a/b does not exist", result.error);

    /*
    tree.write("/c", "foo");
    result = tree.read("/c/d", contents);
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("Parent /c of /c/d is a file", result.error);
     */
}

TEST_F(TreeTreeTest, normalLookup_const)
{
    Tree& constTree = tree;
    std::string contents;
    Result result;
    result = constTree.read("/a/b", contents);
    EXPECT_EQ(Status::LOOKUP_ERROR, result.status);
//    EXPECT_EQ("Parent /a of /a/b does not exist", result.error);

    /*
    tree.write("/c", "foo");
    result = constTree.read("/c/d", contents);
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("Parent /c of /c/d is a file", result.error);
     */
}


TEST_F(TreeTreeTest, mkdirLookup)
{
    /*
    std::string contents;
    Result result;
    tree.write("/c", "foo");
    result = tree.makeDirectory("/c/d");
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("Parent /c of /c/d is a file", result.error);
     */
}

TEST_F(TreeTreeTest, checkCondition)
{
    /*
    tree.write("/a", "b");
    EXPECT_OK(tree.checkCondition("/a", "b"));
    Result result;
    result = tree.checkCondition("/c", "d");
    EXPECT_EQ(Status::CONDITION_NOT_MET, result.status);
    EXPECT_EQ("Could not read value at path '/c': /c does not exist",
              result.error);
    result = tree.checkCondition("/a", "d");
    EXPECT_EQ(Status::CONDITION_NOT_MET, result.status);
    EXPECT_EQ("Path '/a' has value 'b', not 'd' as required",
              result.error);

    EXPECT_OK(tree.checkCondition("/x", ""));
    EXPECT_OK(tree.makeDirectory("/c"));
    result = tree.checkCondition("/c", "");
    EXPECT_EQ(Status::CONDITION_NOT_MET, result.status);
    EXPECT_EQ("Could not read value at path '/c': /c is a directory",
              result.error);
              */
}

TEST_F(TreeTreeTest, makeDirectory)
{
    /*
    EXPECT_OK(tree.makeDirectory("/"));
    EXPECT_EQ("/", dumpTree(tree));

    EXPECT_OK(tree.makeDirectory("/a/"));
    EXPECT_OK(tree.makeDirectory("/a/nodir/b"));
    EXPECT_EQ("/ /a/ /a/nodir/ /a/nodir/b/", dumpTree(tree));

    EXPECT_EQ(Status::INVALID_ARGUMENT, tree.makeDirectory("").status);

    EXPECT_OK(tree.write("/c", "foo"));
    EXPECT_EQ(Status::TYPE_ERROR, tree.makeDirectory("/c/b").status);

    Result result;
    result = tree.makeDirectory("/c");
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("/c already exists but is a file", result.error);
     */
}

TEST_F(TreeTreeTest, listDirectory)
{
    /*
    std::vector<std::string> children;
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              tree.listDirectory("", children).status);
    EXPECT_OK(tree.listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{ }), children);

    EXPECT_OK(tree.makeDirectory("/a/"));
    EXPECT_OK(tree.write("/b", "foo"));
    EXPECT_OK(tree.makeDirectory("/c"));
    EXPECT_OK(tree.write("/d", "foo"));
    EXPECT_OK(tree.listDirectory("/", children));
    EXPECT_EQ((std::vector<std::string>{
                    "a/", "c/", "b", "d",
               }), children);

    Result result;
    result = tree.listDirectory("/e", children);
    EXPECT_EQ(Status::LOOKUP_ERROR, result.status);
    EXPECT_EQ("/e does not exist", result.error);
    result = tree.listDirectory("/d", children);
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("/d is a file", result.error);
     */
}

TEST_F(TreeTreeTest, removeDirectory)
{
    /*
    EXPECT_EQ(Status::INVALID_ARGUMENT, tree.removeDirectory("").status);

    EXPECT_OK(tree.removeDirectory("/a/"));
    EXPECT_OK(tree.removeDirectory("/b"));
    EXPECT_EQ("/", dumpTree(tree));

    EXPECT_OK(tree.makeDirectory("/a/b"));
    EXPECT_OK(tree.write("/a/b/c", "foo"));
    EXPECT_OK(tree.write("/d", "foo"));
    EXPECT_OK(tree.removeDirectory("/a"));

    Result result;
    result = tree.removeDirectory("/d");
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("/d is a file", result.error);
    EXPECT_EQ("/ /d", dumpTree(tree));

    EXPECT_OK(tree.removeDirectory("/"));
    EXPECT_EQ("/", dumpTree(tree));
     */
}

TEST_F(TreeTreeTest, lpush)
{
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    long now = timeSpec.tv_sec;
    EXPECT_OK(tree.lpush("/r", "foo1", now));
    EXPECT_OK(tree.lpush("/r", "foo2", now));
    std::string contents;
    EXPECT_OK(tree.read("/r", contents));
    //FIXME:this case is for current testing purpos,
    // we should use lrange to read rpushed result
    EXPECT_EQ("/r:l:0099998:foo2,/r:l:0099999:foo1,", contents);

    EXPECT_OK(tree.lrem("/r", "foo2", 0, now));
    EXPECT_OK(tree.read("/r", contents));
    //FIXME:this case is for current testing purpos,
    // we should use lrange to read rpushed result
    EXPECT_EQ("/r:l:0099999:foo1,", contents);

    std::string popedResult;
    EXPECT_OK(tree.lpop("/r", popedResult, now));
    Result result = tree.read("/r", contents);
    EXPECT_EQ(Status::LOOKUP_ERROR, result.status);
}


TEST_F(TreeTreeTest, rpush)
{
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    long now = timeSpec.tv_sec;
    EXPECT_OK(tree.rpush("/r", "foo1", now));
    EXPECT_OK(tree.rpush("/r", "foo2", now));
    std::string contents;
    EXPECT_OK(tree.read("/r", contents));
    //FIXME:this case is for current testing purpos,
    // we should use lrange to read rpushed result
    EXPECT_EQ("/r:l:0100000:foo1,/r:l:0100001:foo2,", contents);

    EXPECT_OK(tree.lrem("/r", "foo2", 0, now));
    EXPECT_OK(tree.read("/r", contents));
    //FIXME:this case is for current testing purpos,
    // we should use lrange to read rpushed result
    EXPECT_EQ("/r:l:0100000:foo1,", contents);

    std::string popedResult;
    EXPECT_OK(tree.lpop("/r", popedResult, now));
    Result result = tree.read("/r", contents);
    EXPECT_EQ(Status::LOOKUP_ERROR, result.status);
}

TEST_F(TreeTreeTest, sadd)
{
    EXPECT_EQ(Status::INVALID_ARGUMENT, tree.sadd("", {""}).status);
//    EXPECT_EQ(Status::TYPE_ERROR, tree.sadd("/", {""}).status);
    EXPECT_OK(tree.sadd("/sadd_test/a", {"foo_a"}));
    EXPECT_OK(tree.sadd("/sadd_test/b", {"foo_b"}));
    std::vector<std::string> contents;
    EXPECT_OK(tree.smembers("/sadd_test/a", contents));
    EXPECT_EQ(1, contents.size());
    EXPECT_EQ("foo_a", contents[ 0 ]);
    EXPECT_OK(tree.smembers("/sadd_test/b", contents));
    EXPECT_EQ(1, contents.size());
    EXPECT_EQ("foo_b", contents[ 0 ]);
}

TEST_F(TreeTreeTest, write)
{
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    long now = timeSpec.tv_sec;
    EXPECT_EQ(Status::INVALID_ARGUMENT, tree.write("", "", now).status);
    EXPECT_EQ(Status::TYPE_ERROR, tree.write("/", "", now).status);
    EXPECT_OK(tree.write("/a", "foo", now));
//    EXPECT_EQ("/ /a", dumpTree(tree));
    std::string contents;
    EXPECT_OK(tree.read("/a", contents));
    EXPECT_EQ("foo", contents);
    EXPECT_OK(tree.write("/a", "bar", now));
    EXPECT_OK(tree.read("/a", contents));
    EXPECT_EQ("bar", contents);

    EXPECT_OK(tree.makeDirectory("/b"));
    Result result;
    result = tree.write("/b", "baz", now);
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("/b is a directory", result.error);
}

TEST_F(TreeTreeTest, read)
{
    std::string contents;
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    long now = timeSpec.tv_sec;
    EXPECT_EQ(Status::INVALID_ARGUMENT, tree.read("", contents).status);
    EXPECT_EQ(Status::TYPE_ERROR, tree.read("/", contents).status);

    EXPECT_OK(tree.write("/a", "foo", now));
    EXPECT_OK(tree.read("/a", contents));
    EXPECT_EQ("foo", contents);

    EXPECT_OK(tree.makeDirectory("/b"));

    Result result;
    result = tree.read("/b", contents);
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
//    EXPECT_EQ(Status::TYPE_ERROR, result.status);
//    EXPECT_EQ("/b is a directory", result.error);

    result = tree.read("/c", contents);
    EXPECT_EQ(Status::LOOKUP_ERROR, result.status);
    EXPECT_EQ("/c does not exist", result.error);
}

TEST_F(TreeTreeTest, removeFile)
{
    EXPECT_EQ(Status::INVALID_ARGUMENT, tree.removeFile("").status);
    EXPECT_EQ(Status::TYPE_ERROR, tree.removeFile("/").status);

    EXPECT_OK(tree.removeFile("/a"));

    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    long now = timeSpec.tv_sec;
    EXPECT_OK(tree.write("/b", "foo", now));
    EXPECT_OK(tree.removeFile("/b"));
    EXPECT_OK(tree.removeFile("/c/d"));

    /*
    EXPECT_OK(tree.makeDirectory("/e"));
    Result result;
    result = tree.removeFile("/e");
    EXPECT_EQ(Status::TYPE_ERROR, result.status);
    EXPECT_EQ("/e is a directory", result.error);
     */
}

TEST_F(TreeTreeTest, lrange)
{
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    std::vector<std::string> ouput;
    long now = timeSpec.tv_sec;
    EXPECT_OK(tree.rpush("/r", "one", now));
    EXPECT_OK(tree.rpush("/r", "two", now));
    EXPECT_OK(tree.rpush("/r", "three", now));

    EXPECT_OK(tree.lrange("/r", "0 0", ouput));
    //FIXME:this case is for current testing purpos,
    // we should use lrange to read rpushed result
    EXPECT_EQ(std::vector<std::string>{ "one" }, ouput);

    EXPECT_OK(tree.lrange("/r", "-3 2", ouput));
    EXPECT_EQ((std::vector<std::string>{ "one" , "two", "three"}), ouput);

    EXPECT_OK(tree.lrange("/r", "-100 100", ouput));
    EXPECT_EQ((std::vector<std::string>{ "one" , "two", "three"}), ouput);

    EXPECT_OK(tree.lrange("/r", "5 10", ouput));
    EXPECT_EQ(std::vector<std::string>{}, ouput);
}

TEST_F(TreeTreeTest, ltrim)
{
    auto timeSpec = Core::Time::makeTimeSpec(Core::Time::SystemClock::now());
    std::vector<std::string> ouput;
    long now = timeSpec.tv_sec;
    EXPECT_OK(tree.rpush("/r", "one", now));
    EXPECT_OK(tree.rpush("/r", "two", now));
    EXPECT_OK(tree.rpush("/r", "three", now));

    EXPECT_OK(tree.ltrim("/r", "1 -1", now));
    EXPECT_OK(tree.lrange("/r", "0 -1", ouput));
    EXPECT_EQ((std::vector<std::string>{"two", "three"}), ouput);

    EXPECT_OK(tree.ltrim("/r", "0 0", now));
    EXPECT_OK(tree.lrange("/r", "0 -1", ouput));
    EXPECT_EQ(std::vector<std::string>{"two"}, ouput);
}
} // namespace LogCabin::Tree::<anonymous>
} // namespace LogCabin::Tree
} // namespace LogCabin
