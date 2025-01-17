/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process_util.h"

#include <ctype.h>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef OS_OS2
#include <process.h>
#endif

#include "base/eintr_wrapper.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "nsLiteralString.h"
#include "mozilla/UniquePtr.h"

#include "prenv.h"
#include "prmem.h"

#ifdef MOZ_WIDGET_GONK
/*
 * AID_APP is the first application UID used by Android. We're using
 * it as our unprivilegied UID.  This ensure the UID used is not
 * shared with any other processes than our own childs.
 */
# include <private/android_filesystem_config.h>
# define CHILD_UNPRIVILEGED_UID AID_APP
# define CHILD_UNPRIVILEGED_GID AID_APP
#else
/*
 * On platforms that are not gonk based, we fall back to an arbitrary
 * UID. This is generally the UID for user `nobody', albeit it is not
 * always the case.
 */
# define CHILD_UNPRIVILEGED_UID 65534
# define CHILD_UNPRIVILEGED_GID 65534
#endif

namespace {

enum ParsingState {
  KEY_NAME,
  KEY_VALUE
};

static mozilla::EnvironmentLog gProcessLog("MOZ_PROCESS_LOG");

}  // namespace

namespace base {

class EnvironmentEnvp
{
public:
  EnvironmentEnvp()
    : mEnvp(PR_DuplicateEnvironment()) {}

  explicit EnvironmentEnvp(const environment_map &em)
  {
    mEnvp = (char **)PR_Malloc(sizeof(char *) * (em.size() + 1));
    if (!mEnvp) {
      return;
    }
    char **e = mEnvp;
    for (environment_map::const_iterator it = em.begin();
         it != em.end(); ++it, ++e) {
      std::string str = it->first;
      str += "=";
      str += it->second;
      size_t len = str.length() + 1;
      *e = static_cast<char*>(PR_Malloc(len));
      memcpy(*e, str.c_str(), len);
    }
    *e = NULL;
  }

  ~EnvironmentEnvp()
  {
    if (!mEnvp) {
      return;
    }
    for (char **e = mEnvp; *e; ++e) {
      PR_Free(*e);
    }
    PR_Free(mEnvp);
  }

  char * const *AsEnvp() { return mEnvp; }

  void ToMap(environment_map &em)
  {
    if (!mEnvp) {
      return;
    }
    em.clear();
    for (char **e = mEnvp; *e; ++e) {
      const char *eq;
      if ((eq = strchr(*e, '=')) != NULL) {
        std::string varname(*e, eq - *e);
        em[varname.c_str()] = &eq[1];
      }
    }
  }

private:
  char **mEnvp;
};

class Environment : public environment_map
{
public:
  Environment()
  {
    EnvironmentEnvp envp;
    envp.ToMap(*this);
  }

  char * const *AsEnvp() {
    mEnvp.reset(new EnvironmentEnvp(*this));
    return mEnvp->AsEnvp();
  }

  void Merge(const environment_map &em)
  {
    for (const_iterator it = em.begin(); it != em.end(); ++it) {
      (*this)[it->first] = it->second;
    }
  }
private:
  std::auto_ptr<EnvironmentEnvp> mEnvp;
};

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               bool wait, ProcessHandle* process_handle, base::ProcessArchitecture arch) {
  return LaunchApp(argv, fds_to_remap, environment_map(),
                   wait, process_handle, arch);
}

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               const environment_map& env_vars_to_set,
               bool wait, ProcessHandle* process_handle,
               ProcessArchitecture arch) {
  return LaunchApp(argv, fds_to_remap, env_vars_to_set,
                   PRIVILEGES_INHERIT,
                   wait, process_handle, arch);
}

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               const environment_map& env_vars_to_set,
               ChildPrivileges privs,
               bool wait, ProcessHandle* process_handle,
               ProcessArchitecture arch) {
  mozilla::UniquePtr<char*[]> argv_cstr(new char*[argv.size() + 1]);
  for (size_t i = 0; i < argv.size(); i++)
    argv_cstr[i] = const_cast<char*>(argv[i].c_str());
  argv_cstr[argv.size()] = NULL;

  // Illegal to allocate memory after fork and before execvp
  InjectiveMultimap fd_shuffle1, fd_shuffle2;
  fd_shuffle1.reserve(fds_to_remap.size());
  fd_shuffle2.reserve(fds_to_remap.size());

  Environment env;
  env.Merge(env_vars_to_set);
  char * const *envp = env.AsEnvp();
  if (!envp) {
    DLOG(ERROR) << "FAILED to duplicate environment for: " << argv_cstr[0];
    return false;
  }

#if !defined(OS_OS2)
  pid_t pid = fork();
  if (pid < 0)
    return false;

  if (pid == 0) {
    for (file_handle_mapping_vector::const_iterator
        it = fds_to_remap.begin(); it != fds_to_remap.end(); ++it) {
      fd_shuffle1.push_back(InjectionArc(it->first, it->second, false));
      fd_shuffle2.push_back(InjectionArc(it->first, it->second, false));
    }

    if (!ShuffleFileDescriptors(&fd_shuffle1))
      _exit(127);

    CloseSuperfluousFds(fd_shuffle2);

    SetCurrentProcessPrivileges(privs);

    execve(argv_cstr[0], argv_cstr.get(), envp);
    // if we get here, we're in serious trouble and should complain loudly
    // NOTE: This is async signal unsafe; it could deadlock instead.  (But
    // only on debug builds; otherwise it's a signal-safe no-op.)
    DLOG(ERROR) << "FAILED TO exec() CHILD PROCESS, path: " << argv_cstr[0];
    _exit(127);
  } else
#else // !defined(OS_OS2)

  // Create requested file descriptor duplicates for the child
  for (file_handle_mapping_vector::const_iterator
      it = fds_to_remap.begin(); it != fds_to_remap.end(); ++it) {
    fd_shuffle1.push_back(InjectionArc(it->first, it->second, false));
    fd_shuffle2.push_back(InjectionArc(it->first, it->second, false));
  }
  if (!ShuffleFileDescriptors(&fd_shuffle1))
    return false;

  // Prohibit inheriting all handles but the created ones
  LONG delta = 0;
  ULONG max_fds = 0;
  DosSetRelMaxFH(&delta, &max_fds);
  for (int fd = 0; fd < (LONG)max_fds; ++fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
      continue;
    InjectiveMultimap::const_iterator j;
    for (j = fd_shuffle2.begin(); j != fd_shuffle2.end(); j++) {
      if (fd == j->dest)
        break;
    }
    if (j != fd_shuffle2.end())
      continue;

    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
  }

  pid_t pid = spawnve(P_NOWAIT, argv_cstr[0], argv_cstr.get(), envp);

  // Undo the above: reenable inheritance and close created duplicates
  for (int fd = 0; fd < (LONG)max_fds; ++fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
      continue;
    InjectiveMultimap::const_iterator j;
    for (j = fd_shuffle2.begin(); j != fd_shuffle2.end(); j++) {
      if (fd == j->dest)
        break;
    }
    if (j != fd_shuffle2.end())
      continue;

    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) & ~FD_CLOEXEC);
  }
  for (InjectiveMultimap::const_iterator i = fd_shuffle2.begin(); i != fd_shuffle2.end(); i++) {
    HANDLE_EINTR(close(i->dest));
  }
  
  if (pid < 0) {
    return false;
  } else
#endif // !defined(OS_OS2)
  {
    gProcessLog.print("==> process %d launched child process %d\n",
                      GetCurrentProcId(), pid);
    if (wait)
      HANDLE_EINTR(waitpid(pid, 0, 0));

    if (process_handle)
      *process_handle = pid;
  }

  return true;
}

bool LaunchApp(const CommandLine& cl,
               bool wait, bool start_hidden,
               ProcessHandle* process_handle) {
  file_handle_mapping_vector no_files;
  return LaunchApp(cl.argv(), no_files, wait, process_handle);
}

#if !defined(OS_OS2)
void SetCurrentProcessPrivileges(ChildPrivileges privs) {
  if (privs == PRIVILEGES_INHERIT) {
    return;
  }

  gid_t gid = CHILD_UNPRIVILEGED_GID;
  uid_t uid = CHILD_UNPRIVILEGED_UID;
#ifdef MOZ_WIDGET_GONK
  {
    static bool checked_pix_max, pix_max_ok;
    if (!checked_pix_max) {
      checked_pix_max = true;
      int fd = open("/proc/sys/kernel/pid_max", O_CLOEXEC | O_RDONLY);
      if (fd < 0) {
        DLOG(ERROR) << "Failed to open pid_max";
        _exit(127);
      }
      char buf[PATH_MAX];
      ssize_t len = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if (len < 0) {
        DLOG(ERROR) << "Failed to read pid_max";
        _exit(127);
      }
      buf[len] = '\0';
      int pid_max = atoi(buf);
      pix_max_ok =
        (pid_max + CHILD_UNPRIVILEGED_UID > CHILD_UNPRIVILEGED_UID);
    }
    if (!pix_max_ok) {
      DLOG(ERROR) << "Can't safely get unique uid/gid";
      _exit(127);
    }
    gid += getpid();
    uid += getpid();
  }
#endif
  if (setgid(gid) != 0) {
    DLOG(ERROR) << "FAILED TO setgid() CHILD PROCESS";
    _exit(127);
  }
  if (setuid(uid) != 0) {
    DLOG(ERROR) << "FAILED TO setuid() CHILD PROCESS";
    _exit(127);
  }
  if (chdir("/") != 0)
    gProcessLog.print("==> could not chdir()\n");
}

#endif // !defined(OS_OS2)

}  // namespace base
