<?php
// $Id$
/**
 * @file
 * A few generic functions for interfacing with Subversion via command line.
 * The API of these functions has been inspired by the SVN PECL extension
 * (http://www.php.net/manual/en/ref.svn.php), but works differently in places.
 * On the one hand, this is due to the incompleteness of the functions in here,
 * and on the other hand there are a few artificial restrictions and
 * complications in the PECL extension's API that we can really do without.
 *
 * @note
 *    These functions can be run *without* Drupal.
 *
 * Copyright 2008 by Jakob Petsovits ("jpetso", http://drupal.org/user/56020)
 * Copyright 2006-2008 by Gavin Mogan ("halkeye", http://drupal.org/user/56779)
 */

/**
 * If subsequent function calls from this file act on a private repository
 * that requires authentication, this function will store username and password
 * for the duration of the current process (in a static variable, that is).
 * Other functions in this file make use of this login information.
 */
function svnlib_set_authentication_info($username, $password) {
  _svnlib_authentication_info(
    array('username' => $username, 'password' => $password)
  );
}

/**
 * Unset any username and password that was previously passed
 * to subversion_set_authentication_info(), so that subsequent repository
 * access will happen anonymously again.
 */
function svnlib_unset_authentication_info() {
  _svnlib_authentication_info(FALSE);
}

/**
 * Append username and password to a $cmd array if authentication info
 * has been set before by the caller.
 */
function _svnlib_add_authentication_info(&$cmd) {
  $auth_info = _svnlib_authentication_info();
  if (isset($auth_info)) {
     return array_merge($cmd, array(
       '--username', escapeshellarg($auth_info['username']),
       '--password', escapeshellarg($auth_info['password']),
     ));
  }
}

/**
 * Write or retrieve the authentication info state, stored in a static variable.
 *
 * @param $info
 *   NULL to retrieve the info, FALSE to unset it, or an array with array keys
 *   'username' and 'password' to remember it for later retrieval.
 */
function _svnlib_authentication_info($info = NULL) {
  static $auth_info = NULL;

  if (!isset($info)) {
    return $auth_info;
  }
  else {
    $auth_info = ($info === FALSE) ? NULL : $info;
    return $auth_info;
  }
}

/**
 * By default, Subversion will be invoked with the 'svn' binary which is
 * alright as long as the binary is in the PATH. If it's not, you can call
 * this function to set a different path to the binary (which will be used
 * until this process finishes, or until a new path is set).
 */
function svnlib_set_svn_binary($svn_binary) {
  _svnlib_svn_binary($svn_binary);
}

/**
 * Write or retrieve the path of the svn binary, stored in a static variable.
 *
 * @param $svn_binary
 *   NULL to retrieve the info, or the path to the binary to remember it
 *   for later retrieval.
 */
function _svnlib_svn_binary($svn_binary = NULL) {
  static $binary = 'svn';

  if (!isset($svn_binary)) {
    return $binary;
  }
  $binary = $svn_binary;
  return $binary;
}

/**
 * Return commit log messages of a repository URL. This function is equivalent
 * to 'svn log -v -r $revision_range $repository_url'.
 *
 * @param $repository_url
 *   The URL of the repository (e.g. 'file:///svnroot/my-repo') or an item
 *   inside that repository (e.g. 'file:///svnroot/my-repo/subdir/hello.php').
 * @param $revision_range
 *   The revision specification that will be passed to 'svn log' as the
 *   '-r' parameter. Examples: '35' for a specific revision, 'HEAD:35' for all
 *   revisions since (and including) r35, or the default parameter 'HEAD:1'
 *   for all revisions of the given URL. If you specify the more recent
 *   revision first (e.g. 'HEAD:1') then it will also be first in the
 *   result array, whereas if you specify the older revision first ('1:HEAD')
 *   then you'll get a result array with an ascending sort, the most
 *   recent revision being the last array element.
 * @param $url_revision
 *   The revision of the URL that should be listed.
 *   This needs to be a single revision, e.g. '35' or 'HEAD'.
 *   For example, if a file was deleted in revision 36, you need to pass '35'
 *   as parameter to get its log, otherwise Subversion won't find the file.
 *
 * @return
 *   An array of detailed information about the revisions that exist in the
 *   given URL at the specified revision or revision range.
 */
function svnlib_log($repository_url, $revision_range = 'HEAD:1', $url_revision = 'HEAD') {
  $cmd = array(
    escapeshellarg(escapeshellcmd(_svnlib_svn_binary())),
    'log',
    '-r', $revision_range,
    '--non-interactive',
    '--xml',
    '-v',
  );
  _svnlib_add_authentication_info($cmd);
  $cmd[] = escapeshellarg($repository_url);
  $cmd[] = "2>&1";

  $return_code = 0;
  exec(implode(' ', $cmd), $output, $return_code);
  if ($return_code != 0) {
    return FALSE; // no such revision(s) found
  }
  $log = implode("\n", $output);

  return _svnlib_parse_log($log);
}

/*
 * Parse the output of 'svn log' into an array of log entries.
 * The output looks something like this (0 to N possible "logentry" elements):
<?xml version="1.0"?>
<log>
  <logentry revision="272">
    <author>jpetso</author>
    <date>2007-04-12T15:01:00.247137Z</date>
    <paths>
      <path action="M">/trunk/lila/kde/scalable/apps/ktorrent.svg</path>
      <path action="A">/trunk/lila/kde/scalable/devices/laptop.svg</path>
      <path copyfrom-path="/trunk/lila/kde/scalable/devices/pda_black.svg"
            copyfrom-rev="270"
            action="A">/trunk/lila/kde/scalable/devices/pda_blue.svg</path>
      <path action="R">/trunk/lila/kde/scalable/devices/ipod_unmount.svg</path>
      <path action="D">/trunk/lila/kde/ChangeLog</path>
    </paths>
    <msg>New laptop icon from the GNOME set, more moderate
    colors in ktorrent.svg, and bits of devices stuff.
    </msg>
  </logentry>
</log>
*/
function _svnlib_parse_log($log) {
  $revisions = array();
  $xml = new SimpleXMLElement($log);

  foreach ($xml->logentry as $logentry) {
    $revision = array();
    $revision['rev'] = intval((string) $logentry['revision']);
    $revision['author'] = (string) $logentry->author;
    $revision['msg'] = rtrim((string) $logentry->msg); // no trailing linebreaks
    $revision['time_t'] = strtotime((string) $logentry->date);
    $paths = array();

    foreach ($logentry->paths->path as $logpath) {
      $path = array(
        'path' => (string) $logpath,
        'action' => (string) $logpath['action'],
      );
      if (!empty($logpath['copyfrom-path'])) {
        $path['copyfrom'] = array(
          'path' => (string) $logpath['copyfrom-path'],
          'rev' => (string) $logpath['copyfrom-rev'],
        );
      }
      $paths[] = $path;
    }
    $revision['paths'] = $paths;
    $revisions[$revision['rev']] = $revision;
  }
  return $revisions;
}

/**
 * Return the contents of a directory (specified as repository URL,
 * optionally at a certain revision) as an array of items. This function
 * is equivalent to 'svn ls $repository_url@$revision'.
 *
 * @param $repository_url
 *   The URL of the repository (e.g. 'file:///svnroot/my-repo') or an item
 *   inside that repository (e.g. 'file:///svnroot/my-repo/subdir').
 * @param $url_revision
 *   The revision of the URL that should be listed.
 *   This needs to be a single revision, e.g. '35' or 'HEAD'.
 *   For example, if a file was deleted in revision 36, you need to pass '35'
 *   as parameter to get its listing, otherwise Subversion won't find the file.
 * @param $recursive
 *   FALSE to retrieve just the direct child items of the current directory,
 *   or TRUE to descend into each subdirectory and retrieve all descendant
 *   items recursively. If $recursive is true then each directory item
 *   in the result array will have an additional array element 'children'
 *   which contains the list entries below this directory, as array keys
 *   in the result array.
 *
 *   If @p $repository_url refers to a file then the @p $recursive parameter
 *   has no effect on the 'svn ls' output and, by consequence, on the
 *   return value.
 *
 * @return
 *   A array of items. If @p $repository_url refers to a file then the array
 *   contains a single entry with this file, whereas if @p $repository_url
 *   refers to a directory then the array contains all items inside this
 *   directory (but not the directory itself).
 */
function svnlib_ls($repository_url, $url_revision = 'HEAD', $recursive = FALSE) {
  $cmd = array(
    escapeshellarg(escapeshellcmd(_svnlib_svn_binary())),
    'ls',
    '--non-interactive',
    '--xml',
  );
  if ($recursive) {
    $cmd[] = '-R';
  }
  _svnlib_add_authentication_info($cmd);
  $cmd[] = escapeshellarg($repository_url .'@'. $url_revision);
  $cmd[] = "2>&1";

  $return_code = 0;
  exec(implode(' ', $cmd), $output, $return_code);
  if ($return_code != 0) {
    return FALSE; // no such item or revision found
  }
  $lists = implode("\n", $output);

  return _svnlib_parse_ls($lists, $recursive);
}

/*
 * Parse the output of 'svn ls' into an array of item entries.
 * The output looks something like this (0 to N possible "entry" elements):
<?xml version="1.0"?>
<lists>
  <list path="file:///home/jakob/repos/svn/lila-theme/tags/svg-utils-0-1/utils/svg-utils/svgcolor-xml">
  <entry kind="dir">
    <name>lila</name>
    <commit revision="257">
      <author>jpetso</author>
      <date>2006-11-29T01:27:47.192716Z</date>
    </commit>
  </entry>
  <entry kind="file">
    <name>lila/lila-blue.xml</name>
    <size>918</size>
    <commit revision="9">
      <author>dgt84</author>
      <date>2004-05-04T21:32:13.000000Z</date>
    </commit>
  </entry>
  </list>
</lists>
*/
function _svnlib_parse_ls($lists, $recursive) {
  $items = array();
  $current_item_stack = array(); // will help us determine hierarchical structures
  $xml = new SimpleXMLElement($lists);

  foreach ($xml->list->entry as $entry) {
    $item = array();
    $item['created_rev'] = intval((string) $entry->commit['revision']);
    $item['last_author'] = (string) $entry->commit->author;
    $item['time_t'] = strtotime((string) $entry->commit->date);
    $relative_path = (string) $entry->name;
    $item['name'] = basename($relative_path);
    $item['type'] = (string) $entry['kind'];

    if ($item['type'] == 'file') {
      $item['size'] = intval((string) $entry->size);
    }

    // When listing recursively, we want to capture the item hierarchy.
    if ($recursive) {
      if ($item['type'] == 'dir') {
        $item['children'] = array();
      }
      if (strpos($relative_path, '/') !== FALSE) { // don't regard top-level items
        $parent_path = dirname($relative_path);
        if (isset($items[$parent_path]) && !in_array($relative_path, $items[$parent_path]['children'])) {
          $items[$parent_path]['children'][] = $relative_path;
        }
      }
    }
    $items[$relative_path] = $item;
  }
  return $items;
}

/**
 * Returns detail information about a directory or file item in the repository.
 * In many cases, svnlib_info() is the better svnlib_ls(), as it retrieves not
 * only item names but also repository root and the path of each item
 * inside the repository.
 *
 * You can also use svnlib_info() to retrieve a former item path if the item
 * has been moved or copied: just pass the current URL and revision together
 * with a past or future revision number as @p $target_revision, and you get
 * the path of the item at that time.
 *
 * This function is equivalent to
 * 'svn info -r $target_revision $repository_url@$url_revision'.
 *
 * @param $repository_urls
 *   The URL of the item (e.g. 'file:///svnroot/my-repo/subdir/hello.php')
 *   or the repository itself (e.g. 'file:///svnroot/my-repo'), as string.
 *   Alternatively, you can also pass an array of multiple URLs.
 * @param $url_revision
 *   The revision of the URL that should be listed.
 *   This needs to be a single revision, e.g. '35' or 'HEAD'.
 *   For example, if a file was deleted in revision 36, you need to pass '35'
 *   as parameter to get its info, otherwise Subversion won't find the file.
 *   In case multiple URLs are passed, this revision applies to each of them.
 * @param $recursive
 *   FALSE to retrieve the information for just the specified URL(s),
 *   or TRUE to retrieve all descendant items as well. If $recursive is TRUE
 *   then each directory item in the result array will have an additional
 *   array element 'children' which contains the list entries below
 *   this directory, as array keys in the result array.
 *
 *   If @p $repository_url refers to a file then the @p $recursive parameter
 *   has no effect on the 'svn info' output and, by consequence, on the
 *   return value.
 *
 * @param $target_revision
 *   The revision specification that will be passed to 'svn info' as the
 *   '-r' parameter. This needs to be a single revision, e.g. '35' or 'HEAD'.
 *   This is handy to track item copies and renames, see the general function
 *   description on how to do that. If you leave this at NULL, the info will be
 *   retrieved at the state of the $url_revision.
 *
 * @return
 *   A array of items that contain information about the items that correspond
 *   the specified URL(s). If @p $repository_url refers to a directory and
 *   @p $recursive is TRUE, the array also includes information about all
 *   descendants of the items that correspond to the specified URL(s).
 */
function svnlib_info($repository_urls, $url_revision = 'HEAD', $recursive = FALSE, $target_revision = NULL) {
  if (!is_array($repository_urls)) { // it's a single URL as a string!
    $repository_urls = array($repository_urls);
  }

  $cmd = array(
    escapeshellarg(escapeshellcmd(_svnlib_svn_binary())),
    'info',
    '--non-interactive',
    '--xml',
  );
  if (isset($target_revision)) {
    $cmd[] = '-r';
    $cmd[] = $target_revision;
  }
  if ($recursive) {
    $cmd[] = '-R';
  }
  _svnlib_add_authentication_info($cmd);
  foreach ($repository_urls as $repository_url) {
    $cmd[] = escapeshellarg($repository_url .'@'. $url_revision);
  }
  $cmd[] = "2>&1";

  $return_code = 0;
  exec(implode(' ', $cmd), $output, $return_code);
  if ($return_code != 0) {
    return FALSE; // no such item or revision found
  }
  $info = implode("\n", $output);

  return _svnlib_parse_info($info, $recursive);
}

/*
 * Parse the output of 'svn info' into an array of item entries.
 * The output looks something like this (same URL as in the 'svn ls' example,
 * also 0 to N possible "entry" elements):
<?xml version="1.0"?>
<info>
  <entry kind="dir" path="svgcolor-xml" revision="275">
    <url>file:///home/jakob/repos/svn/lila-theme/tags/svg-utils-0-1/utils/svg-utils/svgcolor-xml</url>
    <repository>
      <root>file:///home/jakob/repos/svn/lila-theme</root>
      <uuid>fd53868f-e4f1-0310-84ca-8663aff3ef64</uuid>
    </repository>
    <commit revision="257">
      <author>jpetso</author>
      <date>2006-11-29T01:27:47.192716Z</date>
    </commit>
  </entry>
  <entry kind="dir" path="lila" revision="275">
    <url>file:///home/jakob/repos/svn/lila-theme/tags/svg-utils-0-1/utils/svg-utils/svgcolor-xml/lila</url>
    <repository>
      <root>file:///home/jakob/repos/svn/lila-theme</root>
      <uuid>fd53868f-e4f1-0310-84ca-8663aff3ef64</uuid>
    </repository>
    <commit revision="257">
      <author>jpetso</author>
      <date>2006-11-29T01:27:47.192716Z</date>
    </commit>
  </entry>
  <entry kind="file" path="lila/lila-blue.xml" revision="275">
    <url>file:///home/jakob/repos/svn/lila-theme/tags/svg-utils-0-1/utils/svg-utils/svgcolor-xml/lila/lila-blue.xml</url>
    <repository>
      <root>file:///home/jakob/repos/svn/lila-theme</root>
      <uuid>fd53868f-e4f1-0310-84ca-8663aff3ef64</uuid>
    </repository>
    <commit revision="9">
      <author>dgt84</author>
      <date>2004-05-04T21:32:13.000000Z</date>
    </commit>
  </entry>
</info>
*/
function _svnlib_parse_info($info, $recursive) {
  $items = array();
  $xml = new SimpleXMLElement($info);

  foreach ($xml->entry as $entry) {
    $item = array();
    $item['url'] = (string) $entry->url;
    $item['repository_root'] = (string) $entry->repository->root;
    $item['repository_uuid'] = (string) $entry->repository->uuid;

    if ($item['url'] == $item['repository_root']) {
      $item['path'] = '/';
    }
    else {
      $item['path'] = substr($item['url'], strlen($item['repository_root']));
    }

    if (isset($items[$item['path']])) {
      // Duplicate item, we had this one before already. Nevertheless, we can
      // perhaps make use of it in order to enhance the hierarchical structure.
      $item = $items[$item['path']];
    }
    else {
      $item['type'] = (string) $entry['kind'];
      $relative_path = (string) $entry['path'];
      $item['rev'] = intval((string) $entry['revision']); // current state of the item
      $item['created_rev'] = intval((string) $entry->commit['revision']); // last edit
      $item['last_author'] = (string) $entry->commit->author;
      $item['time_t'] = strtotime((string) $entry->commit->date);

      if ($recursive && $item['type'] == 'dir') {
        $item['children'] = array();
      }
    }

    if ($recursive && $item['path'] != '/') {
      $parent_path = dirname($item['path']);
      if (isset($items[$parent_path]) && !in_array($item['path'], $items[$parent_path]['children'])) {
        $items[$parent_path]['children'][] = $item['path'];
      }
    }
    $items[$item['path']] = $item;
  }
  return $items;
}