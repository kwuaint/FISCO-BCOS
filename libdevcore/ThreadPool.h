/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief: threadpool obtained from libchannelserver/ThreadPool.h of FISCO-BCOS
 *
 * @file ThreadPool.h
 * @author: yujiechen
 * @date 2018-10-20
 */

#pragma once
#include "easylog.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <iosfwd>
#include <memory>

namespace dev
{
class ThreadPool
{
public:
    typedef std::shared_ptr<ThreadPool> Ptr;

    explicit ThreadPool(const std::string& threadName, size_t size) : _work(_ioService)
    {
        _threadName = threadName;

        for (size_t i = 0; i < size; ++i)
        {
#if 0
			_workers.create_thread(
					boost::bind(&boost::asio::io_service::run, &_ioService));
#endif

            _workers.create_thread([&] {
                dev::pthread_setThreadName(_threadName);
                _ioService.run();
            });
        }
    }
    void stop()
    {
        _ioService.stop();
        _workers.join_all();
    }
    ~ThreadPool() { stop(); }

    // Add new work item to the pool.
    template <class F>
    void enqueue(F f)
    {
        _ioService.post(f);
    }

private:
    std::string _threadName;
    boost::thread_group _workers;
    boost::asio::io_service _ioService;
    boost::asio::io_service::work _work;
};

}  // namespace dev
