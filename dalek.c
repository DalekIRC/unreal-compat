/*
  Licence: GPLv3
  Copyright 2022 Ⓒ Valerie Pond
  Dalek
  
  UnrealIRCd integration with Dalek, with integration with WordPress
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/client-unrealircd-mods/blob/main/nicklock/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/nicklock\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"
#define MSG_PRIVATTEMPT "PRIVATTEMPT"
#define MSG_SPRIVMSG "SPRIVMSG"
#define MSG_MAIL "MAIL"

ModuleHeader MOD_HEADER = {
	"third/dalek",
	"1.0.0",
	"Dalek IRC Services integration",
	"Valware",
	"unrealircd-6",
};


int loggedinfrom_whois(Client *requester, Client *acptr, NameValuePrioList **list);
CMD_OVERRIDE_FUNC(privmsg_ovr);
CMD_FUNC(cmd_privattempt);
CMD_FUNC(cmd_sprivmsg);
CMD_FUNC(cmd_mail);

MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);
	CommandAdd(modinfo->handle, MSG_PRIVATTEMPT, cmd_privattempt, 2, CMD_SERVER|CMD_USER);
	CommandAdd(modinfo->handle, MSG_SPRIVMSG, cmd_sprivmsg, 3, CMD_SERVER);
	CommandAdd(modinfo->handle, MSG_MAIL, cmd_mail, 2, CMD_SERVER|CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_WHOIS, 0, loggedinfrom_whois);
	return MOD_SUCCESS;
}
/** Called upon module load */
MOD_LOAD()
{
	CommandOverrideAdd(modinfo->handle, "PRIVMSG", 0, privmsg_ovr);
	return MOD_SUCCESS;
}

/** Called upon unload */
MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

/**
 * Command override for PRIVMSG
 * If the user is not online, notify dalek about it if they're online.
 * Dalek will check if it's a registered user, and if so, offer the user
 * to use another command /MESSAGE which will let the user send an offline message to the user.
 * Note: This privmsg interception does NOT forward the message. It only notifies Dalek with
 * three peices of information:
 * - The fact you tried to use PRIVMSG
 * - The sender
 * - The recipient
 * Dalek will only recieve this notification if the target is offline or does not exist.
 * 
 * Below, parv[1] references the target nick
*/
CMD_OVERRIDE_FUNC(privmsg_ovr)
{
	Client *target, *services;

	if (!(services = find_server(iConf.services_name, NULL))) // no potential services
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	
	if (parc < 2 || *parv[1] == '\0') // Didn't send anything!
	{
		sendnumeric(client, ERR_NORECIPIENT, ovr->command->cmd);
		return;
	}
	if (strstr(parv[1],",") || *parv[1] == '#') // we don't deal with multiple targets or channels, let original function deal with it
	{
	   	CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	if ((target = find_user(parv[1], NULL))) // client is online, don't touch it, let original function deal with it
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
	        return;
	}
	/* So the user is not online. This is where we come in.
	 * At this point we've already confirmed our services is online and that the user the client
	 * is trying to message is offline. So, ask services to let them know about it.
	*/
	sendto_server(NULL, 0, 0, recv_mtags, ":%s %s %s %s", me.name, MSG_PRIVATTEMPT, client->id, parv[1]);
	
	return;
}

CMD_FUNC(cmd_privattempt)
{
	/* We checked before, but make sure services is still online as it gets passed down the line... */
	Client *services = find_server(iConf.services_name, NULL);
	if (!services)
	{
		unreal_log(ULOG_INFO, "privattempt", "PRIVATTEMPT_COMMAND", client, "PRIVATTEMPT: $client sent PRIVATTEMPT but we had no services to forward to.");
		return;
	}
	/* It's not for us, it's for Dalek - forward it =] */
	sendto_server(client, 0, 0, recv_mtags, ":%s %s %s %s", client->id, MSG_PRIVATTEMPT, parv[1], parv[2]);
	return;
}

/**
 * parv[1] = client
 * parv[2] = target
 * parv[3] = :message
*/
CMD_FUNC(cmd_sprivmsg)
{
	if (!IsULine(client))
		return;

	Client *target;

	if (parc < 4) // invalid
		return;

	if (!(target = find_user(parv[2], NULL)))
		return;

	if (!MyUser(target))
	{
		sendto_one(target->uplink, recv_mtags, ":%s SPRIVMSG %s %s :%s", client->id, parv[1], target->name, parv[3]);
		return;
	}
	sendto_one(target, recv_mtags, ":%s PRIVMSG %s :%s", parv[1], target->name, parv[3]);
}

CMD_FUNC(cmd_mail)
{
	Client *services = find_server(iConf.services_name, NULL);
	if (!services)
	{
		unreal_log(ULOG_INFO, "mail", "CMD_MAIL", client, "MAIL: Services not online.");
		sendto_one(client, NULL, "FAIL MAIL SERVICES_OFFLINE :No mail server online.");
		return;
	}

	if (!client->user->account)
	{
		sendto_one(client, NULL, "FAIL MAIL NOT_LOGGED_IN :Must be logged in to perform that command.");
		return;
	}

	if (parv[1] && !strcasecmp(parv[1],"-list"))
	{
		sendto_one(services, recv_mtags, ":%s %s %s :", client->id, MSG_MAIL, parv[1]);
		return;
	}
	if (parc < 3)
	{
		sendto_one(client, NULL, "FAIL MAIL NEED_MORE_PARAMS :Not enough parameters.");
		sendto_one(client, NULL, "NOTE MAIL SYNTAX :Syntax: /MAIL <accountname> <message>");
		return;
	}
	Client *target = find_user(parv[1], NULL);
	if (target && IsLoggedIn(target) && !strcasecmp(target->user->account, target->name))
	{
		/* Target online, send to them instead */
		sendto_one(client, NULL, "WARN MAIL MESSAGE_REDIRECTED %s :A user is online with that account. Sent as a normal message.", target->name);
		do_cmd(client, recv_mtags, "PRIVMSG", 3, parv);
		sendto_one(client, recv_mtags, ":%s!%s@%s PRIVMSG %s :%s", target->name, target->user->username, (target->umodes & UMODE_SETHOST) ? target->user->virthost : target->user->cloakedhost, client->name, "[*** Redirected ***]");
		return;
	}
	sendto_one(services, recv_mtags, ":%s %s %s :%s", client->id, MSG_MAIL, parv[1], parv[2]);
	return;
}

int loggedinfrom_whois(Client *requester, Client *acptr, NameValuePrioList **list)
{
	Client *client;
	char buf[512];
	
	if (!IsOper(requester) && acptr != requester) // only show to the self
		return 0;
	
	int i = 1;
	list_for_each_entry(client, &client_list, client_node)
	{
		if (!strcasecmp(client->user->account,acptr->user->account))
		{
			add_nvplist_numeric_fmt(list, 999900 + i, "loggedin", acptr, 320, "%s :is logged in from %s!%s@%s (%i)%s", acptr->name, client->name, client->user->username, client->ip, i, (client == requester) ? " (You)" : "");
			++i;
		}
	}
	
	return 0;
}
