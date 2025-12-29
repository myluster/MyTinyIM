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
