// Copyright (c) 2022 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#ifndef BOOST_PROCESS_V2_POSIX_VFORK_LAUNCHER_HPP
#define BOOST_PROCESS_V2_POSIX_VFORK_LAUNCHER_HPP

#include <boost/process/v2/posix/default_launcher.hpp>
#include <unistd.h>

BOOST_PROCESS_V2_BEGIN_NAMESPACE

namespace posix
{


/// A launcher using vfork instead of fork. 
struct vfork_launcher :  default_launcher
{
    vfork_launcher() = default;

    template<typename ExecutionContext, typename Args, typename ... Inits>
    auto operator()(ExecutionContext & context,
                    const typename std::enable_if<std::is_convertible<
                            ExecutionContext&, BOOST_PROCESS_V2_ASIO_NAMESPACE::execution_context&>::value,
                            filesystem::path >::type & executable,
                    Args && args,
                    Inits && ... inits ) -> basic_process<typename ExecutionContext::executor_type>
    {
        error_code ec;
        auto proc =  (*this)(context, ec, executable, std::forward<Args>(args), std::forward<Inits>(inits)...);

        if (ec)
            asio::detail::throw_error(ec, "default_launcher");

        return proc;
    }


    template<typename ExecutionContext, typename Args, typename ... Inits>
    auto operator()(ExecutionContext & context,
                    error_code & ec,
                    const typename std::enable_if<std::is_convertible<
                            ExecutionContext&, BOOST_PROCESS_V2_ASIO_NAMESPACE::execution_context&>::value,
                            filesystem::path >::type & executable,
                    Args && args,
                    Inits && ... inits ) -> basic_process<typename ExecutionContext::executor_type>
    {
        return (*this)(context.get_executor(), executable, std::forward<Args>(args), std::forward<Inits>(inits)...);
    }

    template<typename Executor, typename Args, typename ... Inits>
    auto operator()(Executor exec,
                    const typename std::enable_if<
                            BOOST_PROCESS_V2_ASIO_NAMESPACE::execution::is_executor<Executor>::value ||
                            BOOST_PROCESS_V2_ASIO_NAMESPACE::is_executor<Executor>::value,
                            filesystem::path >::type & executable,
                    Args && args,
                    Inits && ... inits ) -> basic_process<Executor>
    {
        error_code ec;
        auto proc =  (*this)(std::move(exec), ec, executable, std::forward<Args>(args), std::forward<Inits>(inits)...);

        if (ec)
            asio::detail::throw_error(ec, "default_launcher");

        return proc;
    }

    template<typename Executor, typename Args, typename ... Inits>
    auto operator()(Executor exec,
                    error_code & ec,
                    const typename std::enable_if<
                            BOOST_PROCESS_V2_ASIO_NAMESPACE::execution::is_executor<Executor>::value ||
                            BOOST_PROCESS_V2_ASIO_NAMESPACE::is_executor<Executor>::value,
                            filesystem::path >::type & executable,
                    Args && args,
                    Inits && ... inits ) -> basic_process<Executor>
    {
        auto argv = this->build_argv_(executable, std::forward<Args>(args));

        ec = detail::on_setup(*this, executable, argv, inits ...);
        if (ec)
        {
            detail::on_error(*this, executable, argv, ec, inits...);
            return basic_process<Executor>(exec);
        }

        auto & ctx = BOOST_PROCESS_V2_ASIO_NAMESPACE::query(
                exec, BOOST_PROCESS_V2_ASIO_NAMESPACE::execution::context);
        ctx.notify_fork(BOOST_PROCESS_V2_ASIO_NAMESPACE::execution_context::fork_prepare);
		prepare_close_all_fds();
        pid = ::vfork();
        if (pid == -1)
        {
            ctx.notify_fork(BOOST_PROCESS_V2_ASIO_NAMESPACE::execution_context::fork_parent);
            detail::on_fork_error(*this, executable, argv, ec, inits...);
            detail::on_error(*this, executable, argv, ec, inits...);

            BOOST_PROCESS_V2_ASSIGN_EC(ec, errno, system_category())
            return basic_process<Executor>{exec};
        }
        else if (pid == 0)
        {
            ec = detail::on_exec_setup(*this, executable, argv, inits...);
            if (!ec)
                close_all_fds(ec);
            if (!ec)
                ::execve(executable.c_str(), const_cast<char * const *>(argv), const_cast<char * const *>(env));
            ::exit(EXIT_FAILURE);
            return basic_process<Executor>{exec};
        }
        ctx.notify_fork(BOOST_PROCESS_V2_ASIO_NAMESPACE::execution_context::fork_parent);

        if (ec)
        {
            detail::on_error(*this, executable, argv, ec, inits...);
            return basic_process<Executor>{exec};
        }

        basic_process<Executor> proc(exec, pid);
        detail::on_success(*this, executable, argv, ec, inits...);
        return proc;

    }
	protected:

    void prepare_close_all_fds()
    {
        std::sort(fd_whitelist.begin(), fd_whitelist.end());
    }

    void close_all_fds(error_code & ec)
    {
        detail::close_all(fd_whitelist, ec);
    }
};


}

BOOST_PROCESS_V2_END_NAMESPACE

#endif //BOOST_PROCESS_V2_POSIX_VFORK_LAUNCHER_HPP
