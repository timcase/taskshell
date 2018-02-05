////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2006 - 2017, Paul Beckingham, Federico Hernandez.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// http://www.opensource.org/licenses/mit-license.php
//
////////////////////////////////////////////////////////////////////////////////

#include <cmake.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdlib.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <unistd.h>
#include <sys/ioctl.h>

#ifdef SOLARIS
#include <sys/termios.h>
#endif

#include <Color.h>
#include <Lexer.h>
#include <shared.h>
#include <format.h>

std::string getResponse (const std::string&);

////////////////////////////////////////////////////////////////////////////////
static unsigned int getWidth ()
{
  // Determine window size.
//  int width = config.getInteger ("defaultwidth");
  static auto width = 0;

  if (width == 0)
  {
    unsigned short buff[4];
    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &buff) != -1)
      width = buff[1];
  }

  return width;
}

////////////////////////////////////////////////////////////////////////////////
static void editTask (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " edit";
  system (command.c_str ());

  command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " modify reviewed:now";
  system (command.c_str ());
  std::cout << "Modified.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void modifyTask (const std::string& uuid)
{
  Color text ("color15 on gray6");
  std::string modifications;
  do
  {
    modifications = getResponse (text.colorize (" Enter modification args [example: +tag -tag /teh/the/ project:X] ") + " ");
  }
  while (modifications == "");

  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " modify " + modifications;
  system (command.c_str ());

  std::cout << "Modified.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void reviewTask (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " modify reviewed:now";
  system (command.c_str ());
  std::cout << "Marked as reviewed.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void completeTask (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " done";
  system (command.c_str ());
  std::cout << "Completed.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void deleteTask (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " delete";
  system (command.c_str ());
  std::cout << "Deleted.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void scheduleToday (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " mod due:today";
  system (command.c_str ());
  std::cout << "Scheduled for today.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void scheduleThisweek (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " mod +thisweek";
  system (command.c_str ());
  std::cout << "Scheduled for thisweek.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static void unscheduleTask (const std::string& uuid)
{
  std::string command = "task rc.confirmation:no rc.verbose:nothing " + uuid + " mod due:";
  system (command.c_str ());
  std::cout << "Unscheduled.\n\n\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static const std::string reviewNothing ()
{
  return "\nThere are no tasks needing review.\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static const std::string reviewStart (
  unsigned int width)
{
  std::string welcome = "The review process is important for keeping your list "
                        "accurate, so you are working on the right tasks.\n"
                        "\n"
                        "For each task you are shown, look at the metadata. "
                        "Determine whether the task needs to be changed (enter "
                        "'e' to edit), or whether it is accurate ('enter' or "
                        "'r' to mark as reviewed). You may skip a task ('s') "
                        "but a skipped task is not considered reviewed.\n"
                        "\n"
                        "You may stop at any time, and resume later right "
                        "where you left off. See 'man tasksh' for more details.";

  std::vector <std::string> lines;
  wrapText (lines, welcome, width, false);
  welcome = join ("\n", lines);

  return "\n" + welcome + "\n\n";
}

////////////////////////////////////////////////////////////////////////////////
static const std::string banner (
  unsigned int current,
  unsigned int total,
  unsigned int width,
  const std::string& message)
{
  std::stringstream progress;
  progress << " ["
           << current
           << " of "
           << total
           << "] ";

  Color progressColor ("color15 on color9");
  Color descColor     ("color15 on gray6");

  std::string composed;
  if (progress.str ().length () + message.length () + 1 < width)
    composed = progressColor.colorize (progress.str ()) +
               descColor.colorize (" " + message +
                 std::string (width - progress.str ().length () - message.length () - 1, ' '));
  else
    composed = progressColor.colorize (progress.str ()) +
               descColor.colorize (" " + message.substr (0, message.length () - 3) + "...");

  return composed + "\n";
}

////////////////////////////////////////////////////////////////////////////////
static const std::string menu ()
{
  return Color ("color15 on gray6").colorize (" (Enter) Mark as reviewed, go (b)ack a task, (s)kip, (e)dit, (m)odify, (t)oday, this(w)eek, (u)nschedule, (c)omplete, (d)elete, (q)uit ") + " ";
}

////////////////////////////////////////////////////////////////////////////////
static void reviewLoop (const std::vector <std::string>& uuids, unsigned int limit, bool autoClear)
{
  auto width = getWidth ();
  unsigned int reviewed = 0;

  // If a limit was specified ('review 10'), then it should override the data
  // set size, if it is smaller.
  unsigned int total = uuids.size ();
  if (limit)
    total = std::min (total, limit);

  if (total == 0)
  {
    std::cout << reviewNothing ();
    return;
  }

  std::cout << reviewStart (width);

  unsigned int current = 0;
  while (current < total &&
         (limit == 0 || reviewed < limit))
  {
    // Run 'info' report for task.
    auto uuid = uuids[current];

    // Display banner for this task.
    std::string dummy;
    std::string description;
    execute ("task",
             {"_get", uuid + ".description"},
             dummy,
             description);

    std::string response;
    bool repeat;
    do
    {
      repeat = false;
      std::cout << banner (current + 1, total, width, Lexer::trimRight (description, "\n"));

      // Use 'system' to run the command and show the output.
      std::string command = "task " + uuid + " information";
      system (command.c_str ());

      // Display prompt, get input.
      response = getResponse (menu ());

           if (response == "e") { editTask (uuid);                                   }
      else if (response == "m") { modifyTask (uuid);          repeat = true;         }
      else if (response == "s") { std::cout << "Skipped\n\n"; ++current;             }
      else if (response == "b") { std::cout << "Back\n\n";    --current; --reviewed; }
      else if (response == "c") { completeTask (uuid);        ++current; ++reviewed; }
      else if (response == "t") { scheduleToday (uuid);       ++current; ++reviewed; }
      else if (response == "w") { scheduleThisweek (uuid);    ++current; ++reviewed; }
      else if (response == "u") { unscheduleTask (uuid);      ++current; ++reviewed; }
      else if (response == "d") { deleteTask (uuid);          ++current; ++reviewed; }
      else if (response == "")  { reviewTask (uuid);          ++current; ++reviewed; }
      else if (response == "r") { reviewTask (uuid);          ++current; ++reviewed; }
      else if (response == "q") { break;                                             }

      else
      {
        std::cout << format ("Command '{1}' is not recognized.", response) << "\n";
      }

      // Note that just hitting <Enter> yields an empty command, which does
      // nothing but advance to the next task.

      if (autoClear)
        std::cout << "\033[2J\033[0;0H";
    }
    while (repeat);

    if (response == "q")
      break;
  }

  std::cout << "\n"
            << format ("End of review. {1} out of {2} tasks reviewed.", reviewed, total)
            << "\n\n";
}

////////////////////////////////////////////////////////////////////////////////
int cmdReview (const std::vector <std::string>& args, bool autoClear)
{
  // Is there a specified limit?
  unsigned int limit = 0;
  if (args.size () == 2)
    limit = strtol (args[1].c_str (), NULL, 10);

  // Configure 'reviewed' UDA, but only if necessary.
  std::string input;
  std::string output;
  auto status = execute ("task", {"_get", "rc.uda.reviewed.type"}, input, output);
  if (status || output != "date\n")
  {
    if (confirm ("Tasksh needs to define a 'reviewed' UDA of type 'date' for all tasks.  Ok to proceed?"))
    {
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "uda.reviewed.type",  "date"},     input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "uda.reviewed.label", "Reviewed"}, input, output);
    }
  }

  // Configure '_reviewed' report, but only if necessary.
  status = execute ("task", {"_get", "rc.report._reviewed.columns"}, input, output);
  if (status || output != "uuid\n")
  {
    if (confirm ("Tasksh needs to define a '_reviewed' report to identify tasks needing review.  Ok to proceed?"))
    {
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._reviewed.description",
                        "Tasksh review report.  Adjust the filter to your needs."                                              }, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._reviewed.columns", "uuid"               }, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._reviewed.sort",    "reviewed+,modified+"}, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._reviewed.filter",
                        "( reviewed.none: or reviewed.before:now-6days ) and ( +PENDING or +WAITING )"                         }, input, output);
    }
  }

  // Obtain a list of UUIDs to review.
  status = execute ("task",
                    {
                      "rc.color=off",
                      "rc.detection=off",
                      "rc._forcecolor=off",
                      "rc.verbose=nothing",
                      "_reviewed"
                    },
                    input, output);

  // Review the set of UUIDs.
  auto uuids = split (Lexer::trimRight (output, "\n"), '\n');
  reviewLoop (uuids, limit, autoClear);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int cmdToday (const std::vector <std::string>& args, bool autoClear)
{
  // Is there a specified limit?
  unsigned int limit = 0;
  if (args.size () == 2)
    limit = strtol (args[1].c_str (), NULL, 10);

  // Configure 'reviewed' UDA, but only if necessary.
  std::string input;
  std::string output;
  auto status = execute ("task", {"_get", "rc.uda.reviewed.type"}, input, output);
  if (status || output != "date\n")
  {
    if (confirm ("Tasksh needs to define a 'reviewed' UDA of type 'date' for all tasks.  Ok to proceed?"))
    {
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "uda.reviewed.type",  "date"},     input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "uda.reviewed.label", "Reviewed"}, input, output);
    }
  }

  // Configure '_today' report, but only if necessary.
  status = execute ("task", {"_get", "rc.report._today.columns"}, input, output);
  if (status || output != "uuid\n")
  {
    if (confirm ("Tasksh needs to define a '_today' report to identify tasks needing review.  Ok to proceed?"))
    {
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._today.description",
                        "Tasksh review report.  Adjust the filter to your needs."                                              }, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._today.columns", "uuid"               }, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._today.sort",    "reviewed+,modified+"}, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._today.filter",
                        "( +thisweek or +next ) and ( +PENDING or +WAITING )"                         }, input, output);
    }
  }

  // Obtain a list of UUIDs to review.
  status = execute ("task",
                    {
                      "rc.color=off",
                      "rc.detection=off",
                      "rc._forcecolor=off",
                      "rc.verbose=nothing",
                      "_today"
                    },
                    input, output);

  // Review the set of UUIDs.
  auto uuids = split (Lexer::trimRight (output, "\n"), '\n');
  reviewLoop (uuids, limit, autoClear);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int cmdThisweek (const std::vector <std::string>& args, bool autoClear)
{
  // Is there a specified limit?
  unsigned int limit = 0;
  if (args.size () == 2)
    limit = strtol (args[1].c_str (), NULL, 10);

  // Configure 'reviewed' UDA, but only if necessary.
  std::string input;
  std::string output;
  auto status = execute ("task", {"_get", "rc.uda.reviewed.type"}, input, output);
  if (status || output != "date\n")
  {
    if (confirm ("Tasksh needs to define a 'reviewed' UDA of type 'date' for all tasks.  Ok to proceed?"))
    {
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "uda.reviewed.type",  "date"},     input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "uda.reviewed.label", "Reviewed"}, input, output);
    }
  }

  // Configure '_thisweek' report, but only if necessary.
  status = execute ("task", {"_get", "rc.report._thisweek.columns"}, input, output);
  if (status || output != "uuid\n")
  {
    if (confirm ("Tasksh needs to define a '_thisweek' report to identify tasks needing review.  Ok to proceed?"))
    {
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._thisweek.description",
                        "Tasksh review report.  Adjust the filter to your needs."                                              }, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._thisweek.columns", "uuid"               }, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._thisweek.sort",    "reviewed+,modified+"}, input, output);
      execute ("task", {"rc.confirmation:no", "rc.verbose:nothing", "config", "report._thisweek.filter",
                        "( +thisweek or +next ) and ( +PENDING or +WAITING )"                         }, input, output);
    }
  }

  // Obtain a list of UUIDs to review.
  status = execute ("task",
                    {
                      "rc.color=off",
                      "rc.detection=off",
                      "rc._forcecolor=off",
                      "rc.verbose=nothing",
                      "_thisweek"
                    },
                    input, output);

  // Review the set of UUIDs.
  auto uuids = split (Lexer::trimRight (output, "\n"), '\n');
  reviewLoop (uuids, limit, autoClear);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
