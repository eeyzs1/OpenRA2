# 真实 World59 兼容夹具

这些文件由修改前程序实际写出，未改写存档头，也未从 World60 人工删段构造。

| 文件 | 来源 | SHA-256 |
|---|---|---|
| `yr-two-cleg-bf-world59.sav` | 本批修改前探针：真实两名军团兵登上战斗要塞，尚未生成攻击目标 | `3143a4487e05cbb7b1b9e9e07a077c75bfd57228c53a44e6aedfc42e38f4db3d` |
| `ra2-three-beams-world59.sav` | 已封存 Temporal 批次的实际三施法者保存 | `98a4688e10a6877d5f025a7036694923ecb4ce14e9c49ce5b58b751c07b7d5af` |
| `yr-three-beams-world59.sav` | 同批实际 YR 三施法者保存 | `84d9da2549ba0995507d51499006139d612cd3c46c6bb13154d2166775d1a117` |

嵌入的 Scene35 不启用 NativeCrew。读取到新建 World 和已经运行 Scene36 的 World 后，均须保留旧合同与校验值。迁移为 World60 时，只允许新增关闭的 CREW 后缀和格式标识；规范化后须与这些原文件逐字节相同。实际协同链及后续 60 tick 另行比较。
