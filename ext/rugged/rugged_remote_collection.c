/*
 * The MIT License
 *
 * Copyright (c) 2014 GitHub, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "rugged.h"

extern VALUE rb_mRugged;
extern VALUE rb_cRuggedRepo;
extern VALUE rb_eRuggedError;
extern VALUE rb_cRuggedRemote;
VALUE rb_cRuggedRemoteCollection;

/*
 *  call-seq:
 *    RemoteCollection.new(repo) -> remotes
 *
 *  Creates and returns a new collection of remotes for the given +repo+.
 */
static VALUE rb_git_remote_collection_initialize(VALUE self, VALUE repo)
{
	rugged_set_owner(self, repo);
	return self;
}

/*
 *  call-seq:
 *    remotes.create_anonymous(url) -> remote
 *
 *  Return a new remote with +url+ in +repository+ , the remote is not persisted:
 *  - +url+: a valid remote url
 *
 *  Returns a new Rugged::Remote object.
 *
 *    @repo.remotes.create_anonymous('git://github.com/libgit2/libgit2.git') #=> #<Rugged::Remote:0x00000001fbfa80>
 */
static VALUE rb_git_remote_collection_create_anonymous(VALUE self, VALUE rb_url)
{
	git_remote *remote;
	git_repository *repo;
	int error;

	VALUE rb_repo = rugged_owner(self);

	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	Check_Type(rb_url, T_STRING);
	rugged_validate_remote_url(rb_url);

	error = git_remote_create_anonymous(
			&remote,
			repo,
			StringValueCStr(rb_url),
			NULL);

	rugged_exception_check(error);

	return rugged_remote_new(rb_repo, remote);
}

/*
 *  call-seq:
 *     remotes.create(name, url) -> remote
 *
 *  Add a new remote with +name+ and +url+ to +repository+
 *  - +url+: a valid remote url
 *  - +name+: a valid remote name
 *
 *  Returns a new Rugged::Remote object.
 *
 *    @repo.remotes.create('origin', 'git://github.com/libgit2/rugged.git') #=> #<Rugged::Remote:0x00000001fbfa80>
 */
static VALUE rb_git_remote_collection_create(VALUE self, VALUE rb_name, VALUE rb_url)
{
	git_remote *remote;
	git_repository *repo;
	int error;

	VALUE rb_repo = rugged_owner(self);

	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	Check_Type(rb_name, T_STRING);

	Check_Type(rb_url, T_STRING);
	rugged_validate_remote_url(rb_url);

	error = git_remote_create(
			&remote,
			repo,
			StringValueCStr(rb_name),
			StringValueCStr(rb_url));

	rugged_exception_check(error);

	return rugged_remote_new(rb_repo, remote);
}

/*
 *  call-seq:
 *    remotes[name] -> remote or nil
 *
 *  Lookup a remote in the collection with the given +name+.
 *
 *  Returns a new Rugged::Remote object or +nil+ if the
 *  remote doesn't exist.
 *
 *    @repo.remotes["origin"] #=> #<Rugged::Remote:0x00000001fbfa80>
 */
static VALUE rb_git_remote_collection_aref(VALUE self, VALUE rb_name)
{
	git_remote *remote;
	git_repository *repo;
	int error;

	VALUE rb_repo = rugged_owner(self);
	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	Check_Type(rb_name, T_STRING);

	error = git_remote_load(&remote, repo, StringValueCStr(rb_name));

	if (error == GIT_ENOTFOUND)
		return Qnil;

	rugged_exception_check(error);

	return rugged_remote_new(rb_repo, remote);
}

static VALUE rb_git_remote_collection__each(VALUE self, int only_names)
{
	git_repository *repo;
	git_strarray remotes;
	size_t i;
	int error = 0;
	int exception = 0;

	VALUE rb_repo;

	if (!rb_block_given_p()) {		
		if (only_names)
			return rb_funcall(self, rb_intern("to_enum"), 1, CSTR2SYM("each_name"));
		else
			return rb_funcall(self, rb_intern("to_enum"), 1, CSTR2SYM("each"));
	}

	rb_repo = rugged_owner(self);
	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_list(&remotes, repo);
	rugged_exception_check(error);

	if (only_names) {
		for (i = 0; !exception && i < remotes.count; ++i) {
			rb_protect(rb_yield, rb_str_new_utf8(remotes.strings[i]), &exception);
		}
	} else {
		for (i = 0; !exception && !error && i < remotes.count; ++i) {
			git_remote *remote;

			if (!(error = git_remote_load(&remote, repo, remotes.strings[i])))
				rb_protect(rb_yield, rugged_remote_new(rb_repo, remote), &exception);
		}
	}

	git_strarray_free(&remotes);

	if (exception)
		rb_jump_tag(exception);

	rugged_exception_check(error);

	return Qnil;
}


/*
 *  call-seq:
 *    remotes.each { |remote| } -> nil
 *    remotes.each -> enumerator
 *
 *  Iterate through all the remotes in the collection's +repository+.
 *
 *  The given block will be called once with a Rugged::Remote
 *  instance for each remote.
 *
 *  If no block is given, an enumerator will be returned.
 */
static VALUE rb_git_remote_collection_each(VALUE self)
{
	return rb_git_remote_collection__each(self, 0);
}

/*
 *  call-seq:
 *    remotes.each_name { |str| } -> nil
 *    remotes.each_name -> enumerator
 *
 *  Iterate through all the remote names in the collection's +repository+.
 *
 *  The given block will be called once with the name of each remote.
 *
 *  If no block is given, an enumerator will be returned.
 */
static VALUE rb_git_remote_collection_each_name(VALUE self)
{
	return rb_git_remote_collection__each(self, 1);
}

/*
 *  call-seq:
 *    remotes.delete(remote) -> nil
 *    remotes.delete(name) -> nil
 *
 *  Delete the specified remote.
 *
 *    repo.remotes.delete("origin")
 *    # Remote no longer exists in the configuration.
 */
static VALUE rb_git_remote_collection_delete(VALUE self, VALUE rb_name_or_remote)
{
	VALUE rb_repo = rugged_owner(self);
	git_repository *repo;
	int error;

	if (rb_obj_is_kind_of(rb_name_or_remote, rb_cRuggedRemote))
		rb_name_or_remote = rb_funcall(rb_name_or_remote, rb_intern("name"), 0);

	if (TYPE(rb_name_or_remote) != T_STRING)
		rb_raise(rb_eTypeError, "Expecting a String or Rugged::Remote instance");

	rugged_check_repo(rb_repo);
	Data_Get_Struct(rb_repo, git_repository, repo);

	error = git_remote_delete(repo, StringValueCStr(rb_name_or_remote));
	rugged_exception_check(error);

	return Qnil;
}

void Init_rugged_remote_collection(void)
{
	rb_cRuggedRemoteCollection = rb_define_class_under(rb_mRugged, "RemoteCollection", rb_cObject);
	rb_include_module(rb_cRuggedRemoteCollection, rb_mEnumerable);

	rb_define_method(rb_cRuggedRemoteCollection, "initialize",       rb_git_remote_collection_initialize, 1);

	rb_define_method(rb_cRuggedRemoteCollection, "[]",               rb_git_remote_collection_aref, 1);

	rb_define_method(rb_cRuggedRemoteCollection, "create",           rb_git_remote_collection_create, 2);
	rb_define_method(rb_cRuggedRemoteCollection, "create_anonymous", rb_git_remote_collection_create_anonymous, 1);

	rb_define_method(rb_cRuggedRemoteCollection, "each",             rb_git_remote_collection_each, 0);
	rb_define_method(rb_cRuggedRemoteCollection, "each_name",        rb_git_remote_collection_each_name, 0);

	rb_define_method(rb_cRuggedRemoteCollection, "delete",           rb_git_remote_collection_delete, 1);
}
