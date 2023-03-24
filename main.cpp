#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <gflags/gflags.h>
#include <cstring>
#include <pthread.h>
#include "ThreadPool.h"
#include "libipc/ipc.h"
#include <mutex>

enum Type {
    REQ,
    RESPONSE,
    CONNECT,
    DISCONNECT,
};

enum Op {
    ACK_CONN,
    MKDIR,
    MKNOD,
};

struct Msg {
    int id;
    Type ty;
    Op op;
    char data[256];
    int ret;
};

DEFINE_string(mode, "handler", "mode");
DEFINE_uint32(op, 1000, "number of operations");
DEFINE_uint32(threads, 1, "number of thread");

int main(int argc, char **argv) {
    google::ParseCommandLineFlags(&argc, &argv, false);
    auto ch = new ipc::channel("ipc", ipc::sender | ipc::receiver);

    if (FLAGS_mode == "handler") {
        ThreadPool pool(FLAGS_threads);
        std::vector<std::pair<ipc::channel *, std::mutex*>> chs;

        chs.emplace_back(ch, new std::mutex);
        chs.resize(FLAGS_threads + 1);
        std::mutex l;

        bool run = true;

        auto process_reqs = [](Msg *m,
                ipc::channel* lch,
                std::mutex* ch_lock,
                std::vector<std::pair<ipc::channel *, std::mutex*>>* chs,
                std::mutex* l,
                bool *run){
            if (m->ty == CONNECT) {
                // ipc
                std::string con_name = std::to_string(m->id);
//                pthread_spin_lock(l);
                l->lock();
                chs->at(m->id + 1) = std::make_pair(new ipc::channel(con_name.c_str(), ipc::sender | ipc::receiver), new std::mutex);
//                chs->emplace_back(new ipc::channel(con_name.c_str(), ipc::sender | ipc::receiver), new std::mutex);
//                pthread_spin_unlock(l);
//                printf("ch size %d", chs->size());
                l->unlock();
                Msg msg{
                        .id = m->id,
                        .ty = RESPONSE,
                        .op = ACK_CONN,
                        .ret = 1,
                };
                std::string buffer((char *) (&msg), sizeof(Msg));
                printf("Connect [%d]\n", m->id);
//                pthread_spin_lock(ch_lock);
                ch_lock->lock();
                if (!lch->send(buffer, 0)) {
                    printf("ack fail");
                }
//                pthread_spin_unlock(ch_lock);
                ch_lock->unlock();
            } else if (m->ty == REQ) {
                //printf("recv req : id[%d], type[%d], op[%d]\n", req->id, req->ty, req->op);
                Msg msg{
                        .id = m->id,
                        .ty = RESPONSE,
                        .op = MKDIR,
                        .ret = 1,
                };
                std::string buffer((char *) (&msg), sizeof(Msg));
//                pthread_spin_lock(ch_lock);
                ch_lock->lock();
                // send失败得处理一下，这里写的很简单
                if (!lch->send(buffer, 0)) {
                    printf("send failed\n");
                }

//                pthread_spin_unlock(ch_lock);
                ch_lock->unlock();
            } else if (m->ty == DISCONNECT) {
                lch->disconnect();
//                pthread_spin_lock(l);
                l->lock();
                if (strcmp(lch->name(), "ipc") == 0) *run = false;
                lch = nullptr;
//                pthread_spin_unlock(l);
                l->unlock();
            } else {
                // do nothing
            }
            delete m;
        };

        int i = 0;
        std::vector<std::future<void>> results;
        while(run) {
            //printf("try recv [%d] from [%s]\n", i, chs[i]->name());
//            pthread_spin_lock(chs[i].second);
            {
                std::lock_guard<std::mutex> lg(l);
                if (chs[i].first == nullptr) {
                    i = (i + 1) % chs.size();
                    continue;
                }
            }

            chs[i].second->lock();
            ipc::buff_t buf = chs[i].first->try_recv();
//            pthread_spin_unlock(chs[i].second);
            chs[i].second->unlock();
            //ipc::buff_t buf = chs[i]->recv(1);
            //ipc::buff_t buf = ch->recv(1);
            if (buf.empty()) {
                for (auto & result : results) {
                    result.wait();
                }
                results.clear();
//                l.lock();
                i = (i + 1) % chs.size();
//                l.unlock();
                continue;
            }
            auto msg = new Msg;
            memcpy((char*)msg, buf.data(), sizeof(Msg));
            //printf("get req\n");

            results.emplace_back(pool.enqueue(process_reqs, msg, chs[i].first, chs[i].second, &chs, &l, &run));

//            process_reqs(msg, chs[i].first, chs[i].second, chs, &l, &run);
            //printf("end enqueue\n");
//            l.lock();
            i = (i + 1) % chs.size();
//            l.unlock();
        }
        for (int i = 0; i < chs.size(); ++i) {
            delete chs[i].first;
            delete chs[i].second;
        }

    } else {
        std::vector<std::thread> ths;
        ths.reserve(FLAGS_threads);
        std::vector<ipc::channel *> chs;
        chs.resize(FLAGS_threads);
        // initialize channel;
        for (int i = 0; i < FLAGS_threads; ++i) {
            Msg msg{
                    .id = i,
                    .ty = CONNECT,
                    .ret = 0,
            };
            std::string buffer((char *) (&msg), sizeof(Msg));
            if (!ch->send(buffer, 0)) {
                printf("Send initialize connection for [%d] to [%s] failed\n", i, ch->name());
            }
            printf("Send initialize connection for [%d] to [%s]\n", i, ch->name());
            ipc::buff_t buf;
            while (true) {
                buf = ch->recv();
                if (!buf.empty()) {
                    auto resp = (Msg *) (buf.data());
//                    if (resp->id == i && resp->ty == RESPONSE && resp->op == ACK_CONN) {
//                        //printf("[%d] recv response : ret[%d]\n",id,  resp->ret);
//                        chs.push_back(new ipc::channel(std::to_string(i).c_str(), ipc::sender | ipc::receiver));
//                        printf("Initialize connection for [%d]\n", i);
//                        break;
//                    } else {
//                        printf("[%d] recv other reqs : id[%d]\n", i,  resp->id);
//                    }

                    chs.at(resp->id) = new ipc::channel(std::to_string(i).c_str(), ipc::sender | ipc::receiver);
                    break;
                } else {
                    printf("failed");
                    if (!ch->send(buffer, 0)) {
                        printf("Send initialize connection for [%d] to [%s] failed\n", i, ch->name());
                    }
                }
            }
        }
        // run bench
        auto bench = [](ipc::channel *lch, int id) {
            ipc::buff_t buf;
            for (int num = 0; num < FLAGS_op / FLAGS_threads; ++num) {
                Msg msg{
                        .id = id,
                        .ty = REQ,
                        .op = MKDIR,
                        .ret = 0,
                };
                std::string buffer((char *) (&msg), sizeof(Msg));
                if (!lch->send(buffer, 0)) {
                    printf("[%d] send msg failed", id);
                }

                while (true) {
                    buf = lch->recv(1);
                    if (!buf.empty()) {
                        auto resp = (Msg *) (buf.data());
                        if (resp->id == id && resp->ty == RESPONSE) {
//                          printf("[%d] recv response : ret[%d]\n",id,  resp->ret);
                            break;
                        } else {
                            printf("[%d] recv other reqs : id[%d], type[%d], op[%d]\n",id,  resp->id, resp->ty, resp->op);
                        }
                    } else {
//                        if (!lch->send(buffer, 0)) {
//                            printf("[%d] send msg failed", id);
//                        }
                    }
                }
                /*if (num % 10000 == 0) {
                    printf("finished %d\r", num);
                }*/
            }
        };
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < FLAGS_threads; ++i) {
            ths.emplace_back(bench, chs[i], i);
        }
        for (int i = 0; i < FLAGS_threads; ++i) {
            ths[i].join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        uint64_t us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("Finish %d operations in %ld us, ops %lf KOPS\n", FLAGS_op, us, FLAGS_op / (double) us * 1000);
        for (int i = 0; i < chs.size(); ++i) {
            Msg msg{
                    .id = i,
                    .ty = DISCONNECT,
                    .op = MKDIR,
                    .ret = 0,
            };
            std::string buffer((char *) (&msg), sizeof(Msg));
            chs[i]->send(buffer, 0);
        }
        Msg msg{
                .id = 0,
                .ty = DISCONNECT,
                .op = MKDIR,
                .ret = 0,
        };
        std::string buffer((char *) (&msg), sizeof(Msg));
        ch->send(buffer, 0);
        delete ch;
        for (int i = 0; i < chs.size(); ++i) {
            if (chs[i]) delete chs[i];
        }
    }
    return 0;
}
