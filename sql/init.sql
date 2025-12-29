CREATE DATABASE IF NOT EXISTS tinyim;
USE tinyim;

-- ========================================================
-- 1. 用户基础表
-- ========================================================
CREATE TABLE IF NOT EXISTS `im_user` (
  `user_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '用户唯一ID',
  `username` VARCHAR(32) NOT NULL DEFAULT '' COMMENT '用户名/账号',
  `password_hash` VARCHAR(128) NOT NULL DEFAULT '' COMMENT '加密后的密码',
  `nickname` VARCHAR(64) NOT NULL DEFAULT '' COMMENT '昵称',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`user_id`),
  UNIQUE KEY `uk_username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户基础表';

-- ========================================================
-- 2. 消息内容表 (Body) - 写扩散模型
-- ========================================================
CREATE TABLE IF NOT EXISTS `im_message_body` (
  `msg_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '消息全局唯一ID', -- 简化起见暂用自增, 生产环境应用雪花算法
  `sender_id` BIGINT UNSIGNED NOT NULL COMMENT '发送者ID',
  `group_id` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '群ID (0=单聊)',
  `msg_type` TINYINT NOT NULL DEFAULT 1 COMMENT '类型: 1-文本',
  `msg_content` TEXT COMMENT '消息内容',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`msg_id`),
  KEY `idx_sender` (`sender_id`),
  KEY `idx_create_time` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息内容全量表';

-- ========================================================
-- 3. 消息信箱表 (Timeline Index) - 写扩散模型
-- ========================================================
CREATE TABLE IF NOT EXISTS `im_message_index` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `owner_id` BIGINT UNSIGNED NOT NULL COMMENT '信箱所属用户ID (接收者)',
  `other_id` BIGINT UNSIGNED NOT NULL COMMENT '会话对象ID (发送者或群ID)',
  `msg_id` BIGINT UNSIGNED NOT NULL COMMENT '关联的消息内容ID',
  `seq_id` BIGINT UNSIGNED NOT NULL COMMENT '序列号 (单调递增)',
  `is_sender` TINYINT NOT NULL DEFAULT 0 COMMENT '0-接收, 1-发送',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_owner_seq` (`owner_id`, `seq_id`), -- 核心索引：同步拉取用
  KEY `idx_owner_other` (`owner_id`, `other_id`, `created_at`) -- 辅助索引：查历史记录用
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户消息信箱表(Timeline)';

-- ========================================================
-- 4. 好友关系表
-- ========================================================
CREATE TABLE IF NOT EXISTS `im_relation` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `user_id` BIGINT UNSIGNED NOT NULL COMMENT 'Owner',
  `friend_id` BIGINT UNSIGNED NOT NULL COMMENT 'Friend',
  `status` TINYINT NOT NULL DEFAULT 1 COMMENT '1-正常, 2-拉黑, 3-删除',
  `remark` VARCHAR(64) DEFAULT '',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_user_friend` (`user_id`, `friend_id`),
  KEY `idx_friend_id` (`friend_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户好友关系表(双向)';

CREATE TABLE IF NOT EXISTS `im_friend_request` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `user_id` BIGINT UNSIGNED NOT NULL COMMENT '申请人',
  `friend_id` BIGINT UNSIGNED NOT NULL COMMENT '目标用户',
  `remark` VARCHAR(64) DEFAULT '',
  `status` TINYINT NOT NULL DEFAULT 0 COMMENT '0-待处理, 1-已同意, 2-已拒绝',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_friend_status` (`friend_id`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友申请记录表';

-- ========================================================
-- 5. 群组表
-- ========================================================
CREATE TABLE IF NOT EXISTS `im_group` (
  `group_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `group_name` VARCHAR(64) NOT NULL DEFAULT '',
  `owner_id` BIGINT UNSIGNED NOT NULL COMMENT '群主ID',
  `announcement` VARCHAR(256) DEFAULT '',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`group_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群组基础表';

CREATE TABLE IF NOT EXISTS `im_group_member` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `group_id` BIGINT UNSIGNED NOT NULL,
  `user_id` BIGINT UNSIGNED NOT NULL,
  `role` TINYINT DEFAULT 1 COMMENT '1-普通成员, 2-群主, 3-管理员',
  `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_group_user` (`group_id`, `user_id`),
  KEY `idx_user_id` (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群成员列表';
