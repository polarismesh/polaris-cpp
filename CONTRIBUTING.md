# 贡献

我们十分期待您的贡献，无论是提问题、修复问题还是提交一个新的特性。

如果您想提交一个新特性，建议先在 issue 中讨论，避免重复开发。

## 工作流

1. `fork`  polaris-cpp 仓库到你的个人账户空间

2. `clone`个人账户下的`polaris-cpp`仓库到本地。如下修改`<username>` 为你的`TGit`账户

   ```bash
   git clone git@git.code.oa.com:<username>/polaris-cpp.git     # 使用ssh（推荐）
   git clone http://git.code.oa.com/<username>/polaris-cpp.git  # 或者使用http
   ```

3. 添加一个名字为`upstream`的`remote`来指向polaris-cpp官方仓库，并更新

   ```bash
   git remote add upstream git@git.code.oa.com:polaris/polaris-cpp.git     # 使用ssh（推荐）
   git remote add upstream http://git.code.oa.com/polaris/polaris-cpp.git  # 或者使用http
   git fetch upstream
   ```

4. 基于官方仓库的master分支创建一个新分支

   ```bash
   git checkout -b feature-xxx upstream/master
   ```

5. 在新分支完成代码修改并提交

   ```bash
   git commit -a
   ```

6. 可以经常`rebase`你的代码修改

   ```bash
   git pull --rebase
   ```

7. 当修改完成后，使用`rebase`对`commit`进行整理

   ```bash
   git rebase -i upstream/master
   ```

   这个命令会打开你的编辑器，并允许你合并和重排你的提交。

8. 提交合并请求

   使用如下命令将修改推送到你fork的项目中

   ```bash
   git push origin feature-xxx
   ```

   打开浏览器，到polaris官方仓库页面创建新的合并请求。源分支选择项目：`<username>/polaris-cpp`和分支：`feature-xxx`。
   目标分支选择项目`polaris/polaris-cpp`和分支：`master`。单击比较两分支，进入提交合并请求页面。

   - 为你的合并请求填写一个有意义的名字
   - 在描述中解释你的修改和你的合并请求所解决的问题。

    提交合并请求时标题使用以下分类进行开头：

    - `fix:`：表示修复Bug
    - `feat:`：表示添加特性
    - `docs`：修改文档时使用
    - `test`：更新测试用例
    - `refactor`：代码重构

9. 处理蓝盾/CodeCC检查问题

    提交代码后，会触发蓝盾配置的MR检查。如果CI流水线未成功，则点击详情查看流水线失败是否和代码有关。
    如和代码提交有关，则代码提交者需要根据CI提示的问题进行相应修改。

    注：如果无法查看，则按照页面提示申请权限

10. 处理CodeCC检查/Code Review提出的问题

   重复步骤5到步骤7来解决Code Review提出的问题。然后再次提交你的修改：

   ```bash
   git push origin [--force] feature-xxx
   ```

   `--force`选项只有在你进行`rebase`导致提交历史被更改时使用。
