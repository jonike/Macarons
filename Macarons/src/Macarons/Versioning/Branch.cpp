#include "mrpch.h"
#include "Branch.h"

#include "Repository.h"
#include "Commit.h"

#include <git2.h>

namespace Macarons
{
	Branch::Branch(git_reference* ref, BranchType type, const Repository& repo) : Reference{ ref }, m_BranchType{ type }, m_Repository{ repo }
	{
	}

	std::string Branch::GetDisplayName() const
	{
		std::string name = GetName();

		// Erase the heads prefix
		std::string toErase = "refs/heads/";
		size_t found = name.find(toErase);

		if (found != std::string::npos)
		{
			return name.erase(found, toErase.length());
		}

		// Erase the remotes prefix
		toErase = "refs/remotes/";
		found = name.find(toErase);

		if (found != std::string::npos)
		{
			return name.erase(found, toErase.length());
		}

		return name;
	}

	bool Branch::IsActive() const
	{
		return git_branch_is_head(m_Reference);
	}

	bool Branch::IsTrackingRemote() const
	{
		// If we have an upstream, then the branch is tracking a remote.
		git_reference* remote;
		return (git_branch_upstream(&remote, m_Reference) == GIT_ERROR_NONE);
	}

	std::optional<Branch> Branch::GetUpstream() const
	{
		git_reference* upstream;

		int result = git_branch_upstream(&upstream, m_Reference);

		if (result == GIT_ENOTFOUND)
		{
			return std::nullopt;
		}

		MR_CORE_ASSERT(result == GIT_ERROR_NONE, "Could not retrieve upstream branch");

		return Branch(upstream, BranchType::Remote, m_Repository);
	}

	void Branch::SetUpstream(const Branch& upstream)
	{
		MR_CORE_ASSERT(upstream.GetBranchType() == BranchType::Remote, "The upstream branch must be remote");

		const char* name = nullptr;

		int error = git_branch_name(&name, upstream.m_Reference);
		MR_CORE_ASSERT(error == GIT_ERROR_NONE, "Could not get branch name");

		error = git_branch_set_upstream(m_Reference, name);
		MR_CORE_ASSERT(error == GIT_ERROR_NONE, "Could not set upstream branch");
	}

	std::vector<Commit> Branch::GetCommits() const
	{
		std::vector<Commit> result;

		git_revwalk* walk;
		git_revwalk_new(&walk, m_Repository.m_Repo);
		git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
		git_revwalk_push_head(walk);

		git_oid oid;

		while (!git_revwalk_next(&oid, walk))
		{
			git_commit* commit;
			git_commit_lookup(&commit, m_Repository.m_Repo, &oid);

			result.push_back(Commit(commit, this));
		}

		git_revwalk_free(walk);

		return result;
	}

	Commit Branch::CreateCommit(const GitUser& author, std::string& message)
	{
		/* Get the repository index */
		git_index* index;
		git_repository_index(&index, m_Repository.m_Repo);

		/* Write the index to a tree index */
		git_oid treeId;
		git_index_write_tree(&treeId, index);

		/* Get the tree */
		git_tree* tree;
		git_tree_lookup(&tree, m_Repository.m_Repo, &treeId);

		/* Get HEAD as a commit object to use as the parent of the commit */
		git_oid parentId;
		git_commit* parent;

		git_reference_name_to_id(&parentId, m_Repository.m_Repo, "HEAD");
		git_commit_lookup(&parent, m_Repository.m_Repo, &parentId);

		/* Create the commit signature */
		git_signature* user;
		
		int error = git_signature_now(&user, author.Name.c_str(), author.Email.c_str());
		MR_CORE_ASSERT(error == GIT_ERROR_NONE, "Could not sign commit");

		/* Create the commit */
		git_oid commitId;

		const git_commit* parents[] = { parent };

		error = git_commit_create(&commitId, m_Repository.m_Repo, "HEAD", user, user, "UTF-8", message.c_str(), tree, 1, parents);
		MR_CORE_ASSERT(error == GIT_ERROR_NONE, "Could not create commit");

		git_commit* commit;
		error = git_commit_lookup(&commit, m_Repository.m_Repo, &commitId);

		/* Free resources */
		git_tree_free(tree);
		git_signature_free(user);

		return Commit(commit, this);
	}

	void Branch::Reset(bool hard)
	{
		std::vector<Commit> commits = GetCommits();

		if (commits.empty())
		{
			return;
		}

		git_commit* lastCommit = commits.back().m_Commit;

		if (hard)
		{
			git_checkout_options opts;
			git_reset(m_Repository.m_Repo, (git_object*)lastCommit, GIT_RESET_HARD, &opts);
		}
		else
		{
			git_reset(m_Repository.m_Repo, (git_object*)lastCommit, GIT_RESET_MIXED, nullptr);
		}
	}
}